// SPDX-License-Identifier: GPL-2.0-or-later
// Privileged deterministic pause in the SAME operation used by public Store.
// Not a production export API or a guest-exclusion certificate.
#include "common/controlled_guest_arena.h"
namespace Common {
struct ControlledGuestArenaTestAccess {
    static auto Begin(const ControlledGuestArena::Owner& owner, size_t offset = 0) {
        return ControlledGuestArena::Begin(owner.state, offset);
    }
    static HostMemory& Backing(ControlledGuestArena& arena) { return arena.state->backing; }
    static auto HoldMappingMutation(ControlledGuestArena& arena) {
        return arena.state->backing.mapping_domain.BeginMutation();
    }
    static std::weak_ptr<void> Lifetime(ControlledGuestArena& arena) { return arena.state; }
    static size_t Registered(ControlledGuestArena& arena) {
        size_t count{};
        for (const auto& slot : arena.state->owners) count += slot.load();
        return count;
    }
};
}
