// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include <vector>
#include "video_core/renderer_vulkan/vk_transfer_content.h"
#include "video_core/renderer_vulkan/vk_transfer_eligibility.h"
#include "video_core/renderer_vulkan/vk_replica_copy_provenance.h"

int main() {
    // Independent byte-validity oracle. Unknown producers use a separate whole-snapshot latch.
    for (unsigned trial = 0; trial < 64; ++trial) {
        Vulkan::TransferEligibility eligibility;
        eligibility.Arm(128);
        std::array<bool, 128> valid;
        valid.fill(true);
        u32 random = trial + 1;
        for (unsigned operation = 0; operation < 32; ++operation) {
            random = random * 1664525 + 1013904223;
            const u64 begin = (random >> 8) % 128;
            const u64 length = 1 + ((random >> 16) % 5);
            eligibility.Revoke(begin, length);
            for (u64 byte = begin; byte < std::min<u64>(128, begin + length); ++byte) valid[byte] = false;
            for (u64 byte = 0; byte < 128; ++byte) {
                assert(eligibility.Contains(byte, 1) == valid[byte]);
                if (byte + 16 <= 128) {
                    bool expected = true;
                    for (u64 k = byte; k < byte + 16; ++k) expected &= valid[k];
                    assert(eligibility.Contains(byte, 16) == expected);
                }
            }
            assert(!eligibility.Contains(0, 0) && !eligibility.Contains(127, 2));
            assert(!eligibility.Contains(~u64{0}, 16));
        }
    }
    Vulkan::TransferEligibility exhausted;
    exhausted.Arm(256);
    for (unsigned i = 0; i < 63; ++i) exhausted.Revoke(2 * i + 1, 1);
    assert(exhausted.Contains(0, 1));
    exhausted.Revoke(127, 1); // 65 fragments exceed the bounded set: fail closed, not a valid prefix.
    assert(!exhausted.Contains(0, 1) && !exhausted.Contains(255, 1));
    exhausted.Revoke(200, ~u64{0});
    assert(!exhausted.Contains(0, 1));
    using Plan = Vulkan::TransferContent;
    using Kind = Plan::Kind;
    using Provenance = Vulkan::ReplicaCopyProvenance;
    using RootSpan = Provenance::RootSpan;
    // An independent byte array is the only overlay implementation used by this oracle.
    const std::array<Plan::Offset,5> target_sizes{64,64,64,64,64};
    std::vector<Plan::Command> target_fills;
    for(u32 slot=0;slot<5;++slot) target_fills.push_back({Kind::Fill,slot,0,0,0,64,slot});
    const auto target_seed=std::make_shared<const Plan>(*Plan::Build(target_sizes,target_fills));
    for(u32 trial=0;trial<64;++trial) {
        std::array<std::array<Plan::Root,64>,5> labels;
        for(u32 slot=0;slot<5;++slot) for(u64 byte=0;byte<64;++byte) labels[slot][byte]={slot,byte};
        const auto parts=[&](u32 slot,u64 begin,u64 end) {
            std::vector<RootSpan> result;
            for(u64 byte=begin;byte<end;++byte) {
                const auto label=labels[slot][byte];
                if(result.empty() || result.back().fill!=label.command || result.back().root_offset+result.back().size!=label.offset)
                    result.push_back({byte,1,label.command,label.offset});
                else ++result.back().size;
            }
            return result;
        };
        const auto complement=[&](u64 begin,u64 size) {
            auto result=parts(2,0,begin);const auto right=parts(2,begin+size,64);
            result.insert(result.end(),right.begin(),right.end());return result;
        };
        const auto overlay=[&](u32 source,u32 destination,u64 source_offset,u64 destination_offset,u64 size) {
            for(u64 byte=0;byte<size;++byte) labels[destination][destination_offset+byte]=labels[source][source_offset+byte];
        };
        const auto check=[&](const Provenance& plan,u32 slot) {
            for(u64 byte=0;byte<64;++byte) {
                const auto label=labels[slot][byte];
                assert(plan.HasRoot(byte,1,label.command,label.offset));
                assert(!plan.HasRoot(byte,1,(label.command+1)%5,label.offset));
            }
        };
        const u64 first_source=(trial/16%4)*4, first_target=16+(trial%4)*4;
        const auto first=std::make_shared<const Provenance>(*Provenance::Build(target_seed,0,1,first_source,first_target,16,64,0,first_source,1,0));
        overlay(0,1,first_source,first_target,16);check(*first,1);
        const u64 second_source=first_target-8,second_target=8+(trial%4)*4;
        const auto second_parts=parts(1,second_source,second_source+32);
        const auto second=std::make_shared<const Provenance>(*Provenance::Build(target_seed,1,2,second_source,second_target,32,64,
            Plan::Unknown,0,2,0,first,second_parts));
        overlay(1,2,second_source,second_target,32);check(*second,2);
        const auto old_target=labels[2];
        const u64 third_source=(trial/4%8)*4,third_target=second_target+4;
        auto third_complements=complement(third_target,16);const auto saved=third_complements;
        const auto third=std::make_shared<const Provenance>(*Provenance::Build(target_seed,3,2,third_source,third_target,16,64,
            3,third_source,Plan::Unknown,0,{}, {},second,third_complements));
        assert(third->Depth()==3 && third->DestinationCopy()==second);
        third_complements[0].fill=Plan::Unknown;third_complements.clear();
        assert(std::ranges::equal(third->DestinationRoots(),saved));
        overlay(3,2,third_source,third_target,16);check(*third,2);
        const auto before_fourth=labels[2];
        const auto fourth_source=first_target-4,fourth_target=second_target+8;
        const auto fourth_parts=parts(1,fourth_source,fourth_source+24);
        const auto fourth_complements=complement(fourth_target,24);
        const auto fourth=Provenance::Build(target_seed,1,2,fourth_source,fourth_target,24,64,Plan::Unknown,0,Plan::Unknown,0,
            first,fourth_parts,third,fourth_complements);
        assert(fourth && fourth->Depth()==4);overlay(1,2,fourth_source,fourth_target,24);check(*fourth,2);
        for(u64 byte=0;byte<64;++byte) {
            assert(second->HasRoot(byte,1,old_target[byte].command,old_target[byte].offset));
            assert(third->HasRoot(byte,1,before_fourth[byte].command,before_fourth[byte].offset));
        }
        assert(!Provenance::Build(target_seed,3,2,third_source,third_target,16,64,3,third_source,Plan::Unknown,0,{}, {},
            std::make_shared<const Provenance>(*fourth),complement(third_target,16)));
    }
    const std::array<Plan::Offset,2> unknown_target_sizes{64,64};
    const std::array<Plan::Command,3> known_sides{{{Kind::Fill,0,0,0,0,64,99},{Kind::Fill,1,0,0,0,8,10},{Kind::Fill,1,0,40,0,24,20}}};
    const auto unknown_center=std::make_shared<const Plan>(*Plan::Build(unknown_target_sizes,known_sides));
    const std::array<RootSpan,2> known_complements{{{0,8,1,0},{40,24,2,0}}};
    const auto replaced=Provenance::Build(unknown_center,0,1,8,8,32,64,0,8,Plan::Unknown,0,{}, {},{},known_complements);
    assert(replaced && replaced->HasRoot(0,8,1,0) && replaced->HasRoot(8,32,0,8) && replaced->HasRoot(40,24,2,0));
    auto unknown_sides=known_sides;unknown_sides[1].destination_offset=4;unknown_sides[1].size=4;
    const auto unknown_left=std::make_shared<const Plan>(*Plan::Build(unknown_target_sizes,unknown_sides));
    assert(!Provenance::Build(unknown_left,0,1,8,8,32,64,0,8,0,0,{}, {},{},known_complements));
    unknown_sides=known_sides;unknown_sides[2].size=20;
    const auto unknown_right=std::make_shared<const Plan>(*Plan::Build(unknown_target_sizes,unknown_sides));
    assert(!Provenance::Build(unknown_right,0,1,8,8,32,64,0,8,0,0,{}, {},{},known_complements));
    // Per-byte oracle derives the declarations independently, including shifted root projections.
    const std::array<Plan::Offset, 5> mixed_sizes{64, 64, 64, 64, 64};
    std::vector<Plan::Command> mixed_fills;
    for (u32 i = 0; i < 5; ++i) mixed_fills.push_back({Kind::Fill, i, 0, 0, 0, 64, i});
    const auto mixed_seed = std::make_shared<const Plan>(*Plan::Build(mixed_sizes, mixed_fills));
    for (u32 trial = 0; trial < 64; ++trial) {
        std::array<std::array<Plan::Root, 64>, 5> labels;
        for (u32 buffer = 0; buffer < 5; ++buffer)
            for (u64 byte = 0; byte < 64; ++byte) labels[buffer][byte] = {buffer, byte};
        std::shared_ptr<const Provenance> prior;
        u64 previous_begin{};
        for (u32 hop = 1; hop <= 4; ++hop) {
            const u64 source_begin = hop == 1 ? (trial / 16 % 4) * 4 : hop == 2 ? previous_begin - 4 : hop == 3 ? 0 : 4;
            const u64 destination_begin = hop == 1 ? 16 + (trial % 4) * 4 : hop == 2 ? (trial / 4 % 8) * 4 : hop == 3 ? 0 : (trial / 8 % 3) * 4;
            const u64 length = hop == 1 ? 16 : hop == 2 ? 24 : hop == 3 ? 64 : 56;
            std::vector<RootSpan> parts;
            for (u64 byte = source_begin; byte < source_begin + length; ++byte) {
                const auto root = labels[hop - 1][byte];
                if (parts.empty() || parts.back().fill != root.command ||
                    parts.back().root_offset + parts.back().size != root.offset)
                    parts.push_back({byte, 1, root.command, root.offset});
                else ++parts.back().size;
            }
            const auto plan = Provenance::Build(mixed_seed, hop - 1, hop, source_begin, destination_begin,
                length, 64, Plan::Unknown, 0, hop, 0, prior, parts);
            assert(plan && std::ranges::equal(plan->SourceRoots(), parts));
            const auto saved_parts = parts;
            parts[0].fill = Plan::Unknown;parts.clear(); // Borrowed declarations never back immutable evidence.
            assert(std::ranges::equal(plan->SourceRoots(), saved_parts));
            for (u64 byte = 0; byte < length; ++byte)
                labels[hop][destination_begin + byte] = labels[hop - 1][source_begin + byte];
            for (u64 byte = 0; byte < 64; ++byte) {
                const auto root = labels[hop][byte];
                assert(plan->HasRoot(byte, 1, root.command, root.offset));
                assert(!plan->HasRoot(byte, 1, (root.command + 1) % 5, root.offset));
            }
            if (hop == 2) {
                assert(saved_parts.size() == 3);
                assert(!plan->HasRoot(destination_begin, length, 0, saved_parts[1].root_offset));
                assert(!plan->HasRoot(destination_begin, length, 1, source_begin));
            }
            prior = std::make_shared<const Provenance>(*plan);
            previous_begin = destination_begin;
        }
    }
    const std::array<Plan::Offset, 2> large_sizes{256, 256};
    const std::array<Plan::Command, 2> large_fills{{{Kind::Fill, 0, 0, 0, 0, 256, 99}, {Kind::Fill, 1, 0, 0, 0, 256, 10}}};
    const auto large_seed = std::make_shared<const Plan>(*Plan::Build(large_sizes, large_fills));
    std::vector<RootSpan> target_capacity{{0,64,1,0}};
    for(u64 byte=128;byte<190;++byte) target_capacity.push_back({byte,1,1,byte});
    target_capacity.push_back({190,66,1,190});
    assert(Provenance::Build(large_seed,0,1,64,64,64,256,0,64,1,0,{}, {},{},target_capacity));
    target_capacity.back().size=65;target_capacity.push_back({255,1,1,255});
    assert(!Provenance::Build(large_seed,0,1,64,64,64,256,0,64,1,0,{}, {},{},target_capacity));
    std::vector<RootSpan> capacity_parts;
    for (u64 byte = 0; byte < Provenance::MaxRootSpans - 1; ++byte) capacity_parts.push_back({byte, 1, 0, byte});
    capacity_parts.push_back({63, 65, 0, 63});
    assert(Provenance::Build(large_seed, 0, 1, 0, 0, 128, 256, 0, 0, 1, 0, {}, capacity_parts));
    capacity_parts.back().size = 64;capacity_parts.push_back({127, 1, 0, 127});
    assert(!Provenance::Build(large_seed, 0, 1, 0, 0, 128, 256, 0, 0, 1, 0, {}, capacity_parts));
    // Unknown bytes cannot be filled by a declaration, even if its adjacent pieces are known.
    const std::array<Plan::Command, 2> partial_fills{{{Kind::Fill, 0, 0, 16, 0, 16, 99}, {Kind::Fill, 1, 0, 0, 0, 256, 10}}};
    const auto partial_seed = std::make_shared<const Plan>(*Plan::Build(large_sizes, partial_fills));
    const std::array<RootSpan, 3> unknown_parts{{{8, 8, 0, 0}, {16, 16, 0, 0}, {32, 8, 0, 16}}};
    assert(!Provenance::Build(partial_seed, 0, 1, 8, 8, 32, 256, 0, 0, 1, 0, {}, unknown_parts));
    // Independent per-byte labels over four imported copies. Root IDs never become hop-local IDs.
    const std::array<Plan::Offset, 6> chain_sizes{64, 64, 64, 64, 64, 64};
    std::vector<Plan::Command> chain_fills;
    for (u32 i = 0; i < 6; ++i) chain_fills.push_back({Kind::Fill, i, 0, 0, 0, 64, i});
    const auto chain_seed = std::make_shared<const Plan>(*Plan::Build(chain_sizes, chain_fills));
    const auto foreign_seed = std::make_shared<const Plan>(*chain_seed);
    for (u32 trial = 0; trial < 64; ++trial) {
        std::array<std::array<Plan::Root, 64>, 6> labels;
        for (u32 buffer = 0; buffer < 6; ++buffer)
            for (u64 byte = 0; byte < 64; ++byte) labels[buffer][byte] = {buffer, byte};
        std::shared_ptr<const Vulkan::ReplicaCopyProvenance> prior;
        u64 previous_begin{}, previous_length{};
        for (u32 hop = 1; hop <= Vulkan::ReplicaCopyProvenance::MaxHops; ++hop) {
            const u64 length = hop == 1 ? 16 : (hop == 2 ? 8 : 4);
            const u64 source_begin = hop == 1 ? (trial / 4 % 8) * 4
                : (hop == 2 && trial % 2 ? 0 : previous_begin + ((trial % 2) * (previous_length - length)));
            const u64 destination_begin = 16 + (trial % 4) * 4;
            const auto root = labels[hop - 1][source_begin];
            const auto plan = Vulkan::ReplicaCopyProvenance::Build(chain_seed, hop - 1, hop,
                source_begin, destination_begin, length, 64, root.command, root.offset, hop, 0, prior);
            assert(plan && plan->Depth() == hop && plan->SourceCopy() == prior);
            for (u64 byte = 0; byte < length; ++byte)
                labels[hop][destination_begin + byte] = labels[hop - 1][source_begin + byte];
            for (u64 byte = 0; byte < 64; ++byte) {
                const auto expected = labels[hop][byte];
                assert(plan->HasRoot(byte, 1, expected.command, expected.offset));
                assert(!plan->HasRoot(byte, 1, (expected.command + 1) % 6, expected.offset));
            }
            assert(!plan->HasRoot(0, 64, root.command, 0));
            if (prior) {
                assert(!Vulkan::ReplicaCopyProvenance::Build(foreign_seed, hop - 1, hop,
                    source_begin, destination_begin, length, 64, root.command, root.offset, hop, 0, prior));
                assert(!Vulkan::ReplicaCopyProvenance::Build(chain_seed, 5, hop,
                    source_begin, destination_begin, length, 64, root.command, root.offset, hop, 0, prior));
            }
            prior = std::make_shared<const Vulkan::ReplicaCopyProvenance>(*plan);
            previous_begin = destination_begin;
            previous_length = length;
        }
        const auto last_root = labels[4][previous_begin];
        assert(!Vulkan::ReplicaCopyProvenance::Build(chain_seed, 4, 5, previous_begin, 16, 4, 64,
            last_root.command, last_root.offset, 5, 0, prior));
    }
    const std::array<Plan::Offset, 3> sizes{64, 64, 64};
    const std::array<Plan::Command, 3> seeded{{
        {Kind::Fill, 0, 0, 0, 0, 64, 99}, {Kind::Fill, 1, 0, 0, 0, 64, 10},
        {Kind::Copy, 2, 0, 0, 0, 64}}};
    const auto seed = Plan::Build(sizes, seeded);
    assert(seed);
    const auto parent_content = std::make_shared<const Plan>(*seed);
    for (unsigned trial = 0; trial < 64; ++trial) {
        const u64 source_begin = (trial % 8) * 4;
        const u64 destination_begin = ((trial / 8) % 8) * 4;
        const auto ancestry = Vulkan::ReplicaCopyProvenance::Build(parent_content, trial % 2 ? 0 : 2, 1,
            source_begin, destination_begin, 16, 64, 0, source_begin, 1, 0);
        assert(ancestry && ancestry->ParentContent() == parent_content);
        for (u64 byte = 0; byte < 64; ++byte) {
            const bool copied = byte >= destination_begin && byte < destination_begin + 16;
            assert(ancestry->HasRoot(byte, 1, copied ? 0 : 1, copied ? source_begin + byte - destination_begin : byte));
            assert(!ancestry->HasRoot(byte, 1, copied ? 1 : 0, byte));
        }
        assert(!ancestry->HasRoot(0, 64, 0, 0) && !ancestry->HasRoot(0, 64, 1, 0));
        assert(!ancestry->HasRoot(0, 0, 1, 0) && !ancestry->HasRoot(~u64{0}, 4, 1, 0));
    }
    assert(!Vulkan::ReplicaCopyProvenance::Build(parent_content, 0, 1, 16, 60, 16, 64, 0, 16, 1, 0));
    assert(!Vulkan::ReplicaCopyProvenance::Build(parent_content, 0, 0, 16, 16, 16, 64, 0, 16, 0, 0));
    const std::array<Plan::Command, 5> operations{{
        {Kind::Fill, 1, 0, 0, 0, 64, 10},
        {Kind::Fill, 0, 0, 0, 0, 64, 99},
        {Kind::Copy, 1, 0, 16, 16, 16},
        {Kind::Fill, 0, 0, 0, 0, 64, 77},
        {Kind::Copy, 2, 1, 8, 8, 32},
    }};
    const auto plan = Plan::Build(sizes, operations);
    assert(plan);
    assert(plan->HasRoot(1, 16, 16, 1, 16));
    assert(plan->HasRoot(1, 0, 16, 0, 0));
    assert(plan->HasRoot(1, 32, 32, 0, 32));
    assert(!plan->HasRoot(1, 0, 64, 1, 0)); // Stale complement is never promoted.
    assert(!plan->HasRoot(1, 16, 16, 3, 16)); // Source overwritten AFTER the copy.
    assert(plan->HasRoot(0, 0, 64, 3, 0));
    assert(plan->HasRoot(2, 8, 8, 0, 8));
    assert(plan->HasRoot(2, 16, 16, 1, 16));
    assert(plan->HasRoot(2, 32, 8, 0, 32));
    assert(!plan->HasRoot(2, 0, 8, 1, 0)); // Initial bytes are Unknown, never inferred.
    assert(plan->WrittenBy(2, 1, 16, 16));
    assert(!plan->WrittenBy(2, 1, 0, 64));
    assert(!plan->WrittenBy(2, 0, 16, 16));
    assert(plan->Commands()[2].source == 0 && plan->Commands()[2].destination == 1);
    assert(plan->Spans(1).size() == 3);
    const auto partial = Plan::Build(sizes, std::span{operations}.subspan(2, 1));
    assert(partial && !partial->HasRoot(1, 16, 16, 0, 16)); // Unknown copied bytes stay Unknown.
    const std::array<Plan::Command, 2> unknown_source{{
        {Kind::Fill, 1, 0, 0, 0, 64, 10}, {Kind::Copy, 1, 0, 16, 16, 16}}};
    const auto overwritten = Plan::Build(sizes, unknown_source);
    assert(overwritten && overwritten->HasRoot(1, 0, 16, 0, 0));
    assert(!overwritten->HasRoot(1, 16, 16, 0, 16)); // Unknown copy erases the prior known source.
    assert(overwritten->HasRoot(1, 32, 32, 0, 32));
    auto bad = operations;
    bad[2].destination_offset = 60;
    assert(!Plan::Build(sizes, bad));
    bad = operations;
    bad[2].destination = 0; // Vulkan overlapping same-buffer copy is refused.
    assert(!Plan::Build(sizes, bad));
    bad = operations;
    bad[0].size = 63;
    assert(!Plan::Build(sizes, bad));
    bad = operations;
    bad[0].kind = static_cast<Kind>(9); // No generic/shader exact-coverage assertion.
    assert(!Plan::Build(sizes, bad));
    // Independent byte-label oracle, including split copy ancestry. No interval
    // splitting/overlay implementation is shared with the production graph.
    std::uint32_t random = 0x372f3;
    const auto next = [&] {random = random * 1664525u + 1013904223u;return random;};
    for (unsigned trial = 0; trial < 32; ++trial) {
        std::array<std::array<Plan::Root, 64>, 3> bytes;
        for (auto& buffer : bytes) buffer.fill({Plan::Unknown, 0});
        std::vector<Plan::Command> history;
        for (unsigned index = 0; index < 16; ++index) {
            const auto destination = next() % 3;
            const auto source = (destination + 1 + next() % 2) % 3;
            const auto size = 4 * (1 + next() % 16);
            const auto destination_offset = 4 * (next() % ((64 - size) / 4 + 1));
            const auto source_offset = 4 * (next() % ((64 - size) / 4 + 1));
            const bool fill = (next() & 0x100u) != 0;
            history.push_back({fill ? Kind::Fill : Kind::Copy, destination, source,
                               destination_offset, source_offset, size, index});
            for (unsigned byte = 0; byte < size; ++byte)
                bytes[destination][destination_offset + byte] = fill
                    ? Plan::Root{index, byte} : bytes[source][source_offset + byte];
            const auto snapshot = Plan::Build(sizes, history);
            assert(snapshot);
            for (unsigned buffer = 0; buffer < 3; ++buffer)
                for (unsigned byte = 0; byte < 64; ++byte) {
                    const auto expected = bytes[buffer][byte];
                    if (expected.command == Plan::Unknown)
                        assert(!snapshot->HasRoot(buffer, byte, 1, 0, 0));
                    else
                        assert(snapshot->HasRoot(buffer, byte, 1, expected.command, expected.offset));
                }
        }
    }
    {
        using Batch=Vulkan::ReplicaCopyProvenance;
        const std::array<Plan::Offset,2> extents{{256,256}};
        const std::array<Plan::Command,2> fills{{{Plan::Kind::Fill,0,0,0,0,256,99},{Plan::Kind::Fill,1,0,0,0,256,10}}};
        auto anchor=std::make_shared<const Plan>(*Plan::Build(extents,fills));
        std::vector<Batch::Region> regions;
        for(unsigned i=0;i<32;++i) regions.push_back({0,i*4,i*4,4,{},{{i*4,2,0,i*4},{i*4+2,2,0,i*4+2}}});
        std::vector<Batch::RootSpan> complement;
        for(unsigned i=128;i<256;i+=2) complement.push_back({i,2,1,i});
        const auto full=Batch::BuildBatch(anchor,1,0,128,256,regions,{},complement);
        assert(full && full->Regions().size()==32 && full->DestinationRoots().size()==64);
        for(unsigned i=0;i<256;++i) assert(full->HasRoot(i,1,i<128?0:1,i));
        regions[0].roots={{0,1,0,0},{1,1,0,1},{2,2,0,2}};
        assert(!Batch::BuildBatch(anchor,1,0,128,256,regions,{},complement)); // 65 aggregate source pieces.
        regions[0].roots={{0,2,0,0},{2,2,0,2}};
        complement[0]={128,1,1,128};complement.insert(complement.begin()+1,{129,1,1,129});
        assert(!Batch::BuildBatch(anchor,1,0,128,256,regions,{},complement)); // 65 complement pieces.
        complement={{132,124,1,132}};regions.push_back({0,128,128,4,{},{{128,4,0,128}}});
        assert(!Batch::BuildBatch(anchor,1,0,132,256,regions,{},complement)); // 33 commands.
    }
    // Independent byte oracle for disjoint overlays and retained internal holes.
    for (unsigned trial=0;trial<128;++trial) {
        using Batch=Vulkan::ReplicaCopyProvenance;
        const std::array<Plan::Offset,3> extents{{64,64,64}};
        const std::array<Plan::Command,4> initialize{{{Kind::Fill,0,0,0,0,64,99},
            {Kind::Fill,1,0,0,0,64,10},{Kind::Fill,2,0,0,0,64,20},{Kind::Copy,0,0,16,0,16}}};
        auto anchor=std::make_shared<const Plan>(*Plan::Build(extents,initialize));
        std::array<std::array<Plan::Root,64>,3> oracle;
        for(unsigned slot=0;slot<3;++slot) for(unsigned b=0;b<64;++b) oracle[slot][b]={slot,b};
        for(unsigned b=16;b<32;++b) oracle[0][b]=oracle[0][b-16];
        std::shared_ptr<const Batch> previous;
        for(unsigned hop=0;hop<4;++hop) {
            std::array<bool,64> written{};
            std::vector<Batch::Region> writes;
            // Always retain alternating blocks; vary sources and the other block positions.
            for(unsigned block=0;block<16;++block) {
                if(block%4==1 || (next()&0x100u)==0) continue;
                const unsigned source=next()%2,begin=block*4;
                std::vector<Batch::RootSpan> roots;
                for(unsigned b=begin;b<begin+4;++b) {
                    const auto label=oracle[source][b];
                    roots.push_back({b,1,label.command,label.offset});written[b]=true;
                }
                writes.push_back({source,begin,begin,4,{},std::move(roots)});
            }
            if(writes.empty()) {--hop;continue;}
            std::vector<Batch::RootSpan> retained;
            for(unsigned b=0;b<64;++b) if(!written[b]) {
                const auto label=oracle[2][b];retained.push_back({b,1,label.command,label.offset});
            }
            const auto begin=writes.front().destination_offset;
            const auto end=writes.back().destination_offset+4;
            auto disjoint_plan=Batch::BuildBatch(anchor,2,begin,end-begin,64,writes,previous,retained);
            assert(disjoint_plan);
            for(const auto& w:writes) for(unsigned b=w.destination_offset;b<w.destination_offset+w.size;++b)
                oracle[2][b]=oracle[w.source][b];
            for(unsigned b=0;b<64;++b) {
                const auto label=oracle[2][b];
                assert(disjoint_plan->HasRoot(b,1,label.command,label.offset));
                assert(!disjoint_plan->HasRoot(b,1,label.command,label.offset+1));
            }
            previous=std::make_shared<const Batch>(std::move(*disjoint_plan));
        }
        assert(previous->Depth()==4);
    }
    {
        // Both internal holes contain multiple roots, at the exact 64-piece complement budget.
        using Batch=Vulkan::ReplicaCopyProvenance;
        const std::array<Plan::Offset,4> extents{{128,128,128,128}};
        const std::array<Plan::Command,6> initialize{{{Kind::Fill,0,0,0,0,128,99},
            {Kind::Fill,1,0,0,0,128,10},{Kind::Fill,2,0,0,0,128,20},{Kind::Fill,3,0,0,0,128,30},
            {Kind::Copy,2,3,24,4,8},{Kind::Copy,2,1,80,12,8}}};
        auto anchor=std::make_shared<const Plan>(*Plan::Build(extents,initialize));
        std::array<Plan::Root,128> oracle;
        for(unsigned b=0;b<128;++b) oracle[b]={2,b};
        for(unsigned b=24;b<32;++b) oracle[b]={3,b-20};
        for(unsigned b=80;b<88;++b) oracle[b]={1,b-68};
        const std::vector<Batch::Region> writes{{0,8,8,8,{},{{8,8,0,8}}},
            {1,48,48,8,{},{{48,8,1,48}}},{0,104,104,8,{},{{104,8,0,104}}}};
        std::vector<Batch::RootSpan> complement;
        for(unsigned b=0;b<128;++b) {
            const bool written=(b>=8&&b<16)||(b>=48&&b<56)||(b>=104&&b<112);
            if(written) continue;
            const auto label=oracle[b];
            if(!complement.empty() && complement.back().offset+complement.back().size==b &&
                complement.back().fill==label.command && complement.back().root_offset+complement.back().size==label.offset)
                ++complement.back().size;
            else complement.push_back({b,1,label.command,label.offset});
        }
        const auto split=[&] {
            for(size_t j=0;j<complement.size();++j) if(complement[j].size>1) {
                const auto before=complement[j];complement[j].size=1;
                complement.insert(complement.begin()+j+1,{before.offset+1,before.size-1,before.fill,before.root_offset+1});
                return;
            }
            assert(false);
        };
        while(complement.size()<64) split();
        const auto boundary=Batch::BuildBatch(anchor,2,8,104,128,writes,{},complement);
        assert(boundary && boundary->DestinationRoots().size()==64);
        for(const auto& region:writes) for(unsigned b=region.destination_offset;b<region.destination_offset+region.size;++b)
            oracle[b]={region.source,b};
        for(unsigned b=0;b<128;++b) assert(boundary->HasRoot(b,1,oracle[b].command,oracle[b].offset));
        split();assert(complement.size()==65);
        assert(!Batch::BuildBatch(anchor,2,8,104,128,writes,{},complement));
    }
    std::cout << "PASS exact transfer content: partial copies, untouched roots, copy-of-copy, "
                 "source overwrite after copy, Unknown bytes, 512 content/2048 eligibility byte-oracle prefixes, "
                 "256 multi-hop/256 mixed-source/256 mixed-target byte-oracle prefixes, 512 disjoint mixed-target byte-oracle prefixes, mixed internal-hole 64/65-piece boundary, batch 32-command/64-piece boundaries, bounds and fail-closed eligibility capacity\n";
}
