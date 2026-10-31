// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "video_core/buffer_cache/physical_write_identity.h"
#include "video_core/renderer_vulkan/vk_replica_copy_provenance.h"

namespace Vulkan {

// A bounded, stateless recipe over explicitly offered sources. Never physical authority.
class BoundedReconciliationPlanner {
public:
    using P = ReplicaCopyProvenance;
    using Index = P::Index;
    using Offset = P::Offset;
    using RootSpan = P::RootSpan;
    enum class State { Candidate, Refused, Unknown };
    enum class Reason {
        None, InvalidRequest, Budget, BackingMismatch, NativeAlias, Partition,
        RootMismatch, ConflictingSources, AmbiguousSources, MissingContent,
        CurrentUnavailable, Remapped, Retired, SynchronizationUnknown,
        CrossQueueUnknown, SubmissionUnknown, Ancestry, Alignment, NoWrites,
        PhysicalGeometry, MappingDomainUnknown, EvidenceUnavailable
    };
    struct Scope {
        std::shared_ptr<const Common::GuestBackingEpoch> backing;
        Offset offset{}, size{};
    };
    struct Replica {
        Index slot{};
        std::shared_ptr<const VideoCommon::PhysicalWriteIdentity> physical;
        std::shared_ptr<const P> receipt;
        // Source: only explicitly offered intervals. Target: complete logical partition.
        std::span<const RootSpan> roots;
    };
    struct Request {
        std::shared_ptr<const TransferContent> anchor;
        Scope scope;
        Index destination{};
        std::span<const Replica> replicas; // 2-3 including the designated target.
    };
    struct Result {
        State state{State::Refused};
        Reason reason{Reason::InvalidRequest};
        std::shared_ptr<const P> content; // Owns existing immutable Copy metadata only, no storage lease.
    };
    template <typename Evidence>
    static Result Build(const Request& request, const Evidence& evidence) {
        const auto refused=[](Reason reason) {return Result{State::Refused,reason,{}};};
        const auto unknown=[](Reason reason) {return Result{State::Unknown,reason,{}};};
        try {
            const auto& scope=request.scope;
            if (!request.anchor || !scope.backing || !scope.size || (scope.offset|scope.size)%4 ||
                scope.size>std::numeric_limits<Offset>::max()-scope.offset || request.replicas.size()<2)
                return refused(Reason::InvalidRequest);
            if (request.replicas.size()>3 || scope.size>VideoCommon::PhysicalWriteIdentity::MaxSegments*4096)
                return refused(Reason::Budget);
            if (!scope.backing->alive.load(std::memory_order_acquire)) return unknown(Reason::Retired);
            const Replica* target{};
            size_t pieces{};
            std::vector<Offset> boundaries{0,scope.size};
            for (size_t i=0;i<request.replicas.size();++i) {
                const auto& replica=request.replicas[i];const auto& physical=replica.physical;
                const auto map=request.anchor->Spans(replica.slot);
                if (!physical || map.empty() || map.back().offset+map.back().size!=scope.size)
                    return refused(Reason::InvalidRequest);
                if (replica.slot==request.destination) target=&replica;
                if (replica.roots.size()>P::MaxRootSpans-pieces) return refused(Reason::Budget);
                pieces+=replica.roots.size();
                if (physical->Backing()!=scope.backing) return refused(Reason::BackingMismatch);
                if (!physical->OwnersAlive()) return unknown(Reason::Retired);
                if (!physical->CapturedEpochCurrent()) return unknown(Reason::Remapped);
                if (physical->DeviceEpoch()!=request.replicas.front().physical->DeviceEpoch())
                    return unknown(Reason::MappingDomainUnknown);
                if (!physical->Representation() || physical->Size()!=scope.size) return refused(Reason::PhysicalGeometry);
                Offset mapped{};
                for (const auto& segment:physical->Segments()) {
                    if (segment.logical_offset!=mapped || !segment.length || segment.length>scope.size-mapped ||
                        segment.physical_offset!=scope.offset+mapped) return refused(Reason::PhysicalGeometry);
                    mapped+=segment.length;
                }
                if (mapped!=scope.size) return refused(Reason::PhysicalGeometry);
                for (size_t j=0;j<i;++j)
                    if (request.replicas[j].slot==replica.slot ||
                        request.replicas[j].physical->Representation()==physical->Representation())
                        return refused(Reason::NativeAlias);
                if (replica.receipt && (replica.receipt->ParentContent()!=request.anchor ||
                    replica.receipt->DestinationBuffer()!=replica.slot || replica.receipt->Depth()>=P::MaxHops))
                    return refused(Reason::Ancestry);
                Offset cursor{};
                for (const auto& part:replica.roots) {
                    if (!part.size || part.offset<cursor || part.offset>=scope.size || part.size>scope.size-part.offset ||
                        part.size>std::numeric_limits<Offset>::max()-part.root_offset ||
                        (replica.slot==request.destination && part.offset!=cursor)) return refused(Reason::Partition);
                    cursor=part.offset+part.size;
                    boundaries.push_back(part.offset);boundaries.push_back(cursor);
                }
                if (replica.slot==request.destination && cursor!=scope.size) return unknown(Reason::MissingContent);
            }
            if (!target) return refused(Reason::InvalidRequest);
            for (const auto& replica:request.replicas)
                if (replica.slot!=request.destination && !replica.roots.empty() &&
                    replica.physical->DeviceAddress()==target->physical->DeviceAddress()) return refused(Reason::PhysicalGeometry);
            std::sort(boundaries.begin(),boundaries.end());
            boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
            const auto locate=[](const Replica& replica,Offset begin)->const RootSpan* {
                for (const auto& part:replica.roots) if (part.offset<=begin && begin-part.offset<part.size) return &part;
                return nullptr;
            };
            const auto check=[&](const Replica& replica,const RootSpan& part,Offset begin,Offset length)->Result {
                if (part.fill==TransferContent::Unknown) return unknown(Reason::MissingContent);
                const RootSpan projection{begin,length,part.fill,part.root_offset+begin-part.offset};
                if (!(replica.receipt ? replica.receipt->HasRoot(begin,length,part.fill,projection.root_offset)
                    : request.anchor->HasRoot(replica.slot,begin,length,part.fill,projection.root_offset)))
                    return refused(Reason::RootMismatch);
                const auto gate=evidence(replica,projection);
                if (gate!=Reason::None) return unknown(gate);
                return {State::Candidate,Reason::None,{}};
            };
            const auto append=[](std::vector<RootSpan>& parts,RootSpan part) {
                if (!parts.empty() && parts.back().offset+parts.back().size==part.offset &&
                    parts.back().fill==part.fill && parts.back().root_offset+parts.back().size==part.root_offset)
                    parts.back().size+=part.size;
                else parts.push_back(part);
            };
            std::vector<P::Region> regions;
            std::vector<RootSpan> complements;
            for (size_t i=1;i<boundaries.size();++i) {
                const auto begin=boundaries[i-1],length=boundaries[i]-begin;
                const Replica* source{};const RootSpan* origin{};size_t sources{};bool conflict{};
                for (const auto& replica:request.replicas) {
                    if (replica.slot==request.destination) continue;
                    const auto* part=locate(replica,begin);if (!part) continue;
                    const auto checked=check(replica,*part,begin,length);
                    if (checked.state!=State::Candidate) return checked;
                    if (source && (origin->fill!=part->fill ||
                        origin->root_offset+begin-origin->offset!=part->root_offset+begin-part->offset)) conflict=true;
                    source=&replica;origin=part;++sources;
                }
                if (sources>1) return conflict ? refused(Reason::ConflictingSources) : unknown(Reason::AmbiguousSources);
                if (!source) {
                    const auto* part=locate(*target,begin);
                    if (!part) return unknown(Reason::MissingContent);
                    const auto checked=check(*target,*part,begin,length);
                    if (checked.state!=State::Candidate) return checked;
                    append(complements,{begin,length,part->fill,part->root_offset+begin-part->offset});
                } else {
                    if (regions.empty() || regions.back().source!=source->slot ||
                        regions.back().source_copy!=source->receipt || regions.back().destination_offset+regions.back().size!=begin)
                        regions.push_back({source->slot,begin,begin,0,source->receipt,{}});
                    regions.back().size+=length;
                    append(regions.back().roots,{begin,length,origin->fill,origin->root_offset+begin-origin->offset});
                }
            }
            if (regions.empty()) return refused(Reason::NoWrites);
            size_t source_pieces{};
            for (const auto& region:regions) {
                if ((region.source_offset|region.destination_offset|region.size)%4) return refused(Reason::Alignment);
                source_pieces+=region.roots.size();
            }
            if (regions.size()>TransferContent::MaxCommands || source_pieces>P::MaxRootSpans || complements.size()>P::MaxRootSpans)
                return refused(Reason::Budget);
            const auto begin=regions.front().destination_offset;
            const auto end=regions.back().destination_offset+regions.back().size;
            auto plan=P::BuildBatch(request.anchor,request.destination,begin,end-begin,scope.size,regions,target->receipt,complements);
            if (!plan) return refused(Reason::Ancestry);
            return {State::Candidate,Reason::None,std::make_shared<const P>(std::move(*plan))};
        } catch (...) {return unknown(Reason::EvidenceUnavailable);}

    }
};
} // namespace Vulkan
