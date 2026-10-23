// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <tuple>
#include <type_traits>
#include <utility>

// Real backend headers can open this namespace before the adapter is parsed.
// The adapter must always refer to the embedder's ::Common, independently of order.
namespace Dynarmic::Common {}

#include "dynarmic/common/container/unordered_map.h"
#include "dynarmic/common/container/unordered_set.h"
#include "dynarmic/ir/location_descriptor.h"

namespace {
using Descriptor = Dynarmic::IR::LocationDescriptor;
using CallbackKey = std::tuple<bool, unsigned long, int, int>;
using LinkKey = std::tuple<Descriptor, unsigned>;
struct ExplicitHash {
    std::size_t operator()(const LinkKey&) const { return 1; }
};
static_assert(std::is_same_v<Common::unordered_map<CallbackKey, int>::hasher,
                             Dynarmic::Compat::DefaultHash<CallbackKey>>);
static_assert(std::is_same_v<Common::unordered_map<LinkKey, int, ExplicitHash>::hasher,
                             ExplicitHash>);
} // namespace

TEST_CASE("Pinned Dynarmic containers retain descriptor and tuple hash contracts", "[common][dynarmic]") {
    Common::unordered_map<Descriptor, int> descriptors;
    descriptors.emplace(Descriptor{42}, 7);
    REQUIRE(descriptors.at(Descriptor{42}) == 7);

    // Actual key shapes in a32/a64 EmitX64: callback trampolines and patch links.
    Common::unordered_map<CallbackKey, int> callbacks;
    callbacks.emplace(CallbackKey{true, 4096, -1, 3}, 11);
    callbacks.emplace(CallbackKey{false, 4096, -1, 3}, 12);
    REQUIRE(callbacks.at(CallbackKey{true, 4096, -1, 3}) == 11);
    REQUIRE(callbacks.at(CallbackKey{false, 4096, -1, 3}) == 12);

    Common::unordered_set<LinkKey> links;
    links.emplace(Descriptor{42}, 1);
    REQUIRE(links.contains(LinkKey{Descriptor{42}, 1}));
    REQUIRE_FALSE(links.contains(LinkKey{Descriptor{43}, 1}));

    Common::unordered_map<std::pair<Descriptor, unsigned>, int> pairs;
    pairs.emplace(std::pair{Descriptor{42}, 2U}, 13);
    REQUIRE(pairs.at(std::pair{Descriptor{42}, 2U}) == 13);

    // A caller-supplied hasher remains authoritative, including deliberate collisions.
    Common::unordered_map<LinkKey, int, ExplicitHash> custom;
    custom.emplace(LinkKey{Descriptor{42}, 1}, 14);
    custom.emplace(LinkKey{Descriptor{43}, 1}, 15);
    REQUIRE(custom.at(LinkKey{Descriptor{42}, 1}) == 14);
    REQUIRE(custom.at(LinkKey{Descriptor{43}, 1}) == 15);
}
