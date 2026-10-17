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
    std::cout << "PASS exact transfer content: partial copies, untouched roots, copy-of-copy, "
                 "source overwrite after copy, Unknown bytes, 512 content/2048 eligibility byte-oracle prefixes, "
                 "256 multi-hop byte-oracle prefixes, bounds and fail-closed eligibility capacity\n";
}
