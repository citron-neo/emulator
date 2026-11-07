// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include "video_core/renderer_vulkan/vk_sync_provenance.h"

int main() {
    using Journal = Vulkan::SyncProvenance;
    const Journal::Access transfer{VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
    const Journal::Access shader{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                           VK_ACCESS_MEMORY_WRITE_BIT,
                           VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    Journal journal;
    const auto a = journal.RecordAccess();
    const auto no_barrier = journal.RecordAccess();
    assert(!journal.Proves(a, transfer, no_barrier, transfer));
    journal.RecordBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, memory);
    const auto wrong_source = journal.RecordAccess();
    assert(!journal.Proves(a, transfer, wrong_source, transfer));
    journal.RecordBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, memory);
    const auto wrong_destination = journal.RecordAccess();
    assert(!journal.Proves(a, transfer, wrong_destination, transfer));
    // A barrier after B cannot establish the earlier A -> B relationship.
    journal.RecordBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, memory);
    assert(!journal.Proves(a, transfer, no_barrier, transfer));
    const auto valid = journal.RecordAccess();
    assert(journal.Proves(a, transfer, valid, shader));
    assert(!journal.Proves(valid, shader, a, transfer));
    assert(!journal.Proves(valid, shader, valid, shader));
    assert(!journal.Proves(a, {}, valid, shader));

    Journal missing_access;
    const auto first = missing_access.RecordAccess();
    memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    missing_access.RecordBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, memory);
    const auto second = missing_access.RecordAccess();
    assert(!missing_access.Proves(first, transfer, second, shader));
    memory.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    memory.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    missing_access.RecordBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, memory);
    const auto third = missing_access.RecordAccess();
    assert(!missing_access.Proves(first, transfer, third, shader));
    memory.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    for (unsigned i = 0; i < 257; ++i)
        journal.RecordBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, memory);
    const auto overflow = journal.RecordAccess();
    assert(!journal.Proves(a, transfer, overflow, shader));
    Vulkan::SyncProvenance imported;
    const Vulkan::SyncProvenance::Access copy_access{VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    const Vulkan::SyncProvenance::Access later_access{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    VkMemoryBarrier entry{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    imported.RecordBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, entry);
    const auto copy_at = imported.RecordAccess();
    assert(imported.ProvesEarlierSubmission(transfer, copy_at, copy_access));
    assert(!imported.ProvesRelease(copy_at, copy_access, later_access));
    entry.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    entry.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    imported.RecordBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, entry);
    assert(imported.ProvesRelease(copy_at, copy_access, later_access));
    assert(!imported.ProvesEarlierSubmission({}, copy_at, copy_access));
    assert(!imported.ProvesRelease(copy_at, copy_access, {}));
    assert(!imported.ProvesEarlierSubmission({VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT}, copy_at, copy_access));
    std::cout << "PASS sync journal: missing/misplaced barrier, wrong stages/access, "
                 "same-command refusal, complete direct edge and overflow\n";
}
