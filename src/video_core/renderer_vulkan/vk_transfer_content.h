// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace Vulkan {

// Immutable, bounded history inside ONE explicitly recorded transfer callback.
// Uninitialized bytes start Unknown. A root names a fill in this callback, not a
// global version, the latest physical writer, or permission to publish content.
class TransferContent {
public:
    using Index = std::uint32_t;
    using Offset = std::uint64_t;
    static constexpr Index Unknown = ~Index{};
    static constexpr Index MaxBuffers = 8, MaxCommands = 32, MaxSpans = 64, MaxNodes = 256;
    enum class Kind { Fill, Copy };
    struct Command {
        Kind kind;
        Index destination{}, source{};
        Offset destination_offset{}, source_offset{}, size{};
        std::uint32_t value{};
    };
    struct Span { Offset offset, size; Index node; Offset node_offset; };
    struct Node { Index command, parent; Offset parent_offset, size; };
    struct Root { Index command; Offset offset; };

    static std::optional<TransferContent> Build(std::span<const Offset> sizes,
                                               std::span<const Command> commands) {
        if (sizes.empty() || sizes.size() > MaxBuffers || commands.empty() || commands.size() > MaxCommands)
            return {};
        TransferContent plan;
        plan.buffer_count = static_cast<Index>(sizes.size());
        plan.command_count = static_cast<Index>(commands.size());
        std::copy(commands.begin(), commands.end(), plan.commands.begin());
        for (Index i = 0; i < plan.buffer_count; ++i) {
            if (!sizes[i]) return {};
            plan.sizes[i] = sizes[i];
            plan.maps[i].spans[0] = {0, sizes[i], Unknown, 0};
            plan.maps[i].count = 1;
        }
        for (Index i = 0; i < plan.command_count; ++i) {
            const auto& operation = commands[i];
            if (operation.destination >= sizes.size() || !operation.size || operation.size % 4 ||
                operation.destination_offset % 4 || operation.destination_offset >= sizes[operation.destination] ||
                operation.size > sizes[operation.destination] - operation.destination_offset) return {};
            Map replacement;
            if (operation.kind == Kind::Fill) {
                if (plan.node_count == MaxNodes) return {};
                replacement.spans[replacement.count++] = {
                    operation.destination_offset, operation.size, plan.node_count, 0};
                plan.nodes[plan.node_count++] = {i, Unknown, 0, operation.size};
            } else if (operation.kind == Kind::Copy) {
                if (operation.source >= sizes.size() || operation.source_offset % 4 ||
                    operation.source_offset >= sizes[operation.source] ||
                    operation.size > sizes[operation.source] - operation.source_offset) return {};
                if (operation.source == operation.destination &&
                    operation.source_offset < operation.destination_offset + operation.size &&
                    operation.destination_offset < operation.source_offset + operation.size) return {};
                for (const auto& source : plan.Spans(operation.source)) {
                    const auto begin = std::max(source.offset, operation.source_offset);
                    const auto end = std::min(source.offset + source.size, operation.source_offset + operation.size);
                    if (begin >= end) continue;
                    if (plan.node_count == MaxNodes || replacement.count == MaxSpans) return {};
                    replacement.spans[replacement.count++] = {
                        operation.destination_offset + begin - operation.source_offset,
                        end - begin, plan.node_count, 0};
                    plan.nodes[plan.node_count++] = {
                        i, source.node, source.node_offset + begin - source.offset, end - begin};
                }
            } else {
                return {};
            }
            if (!Install(plan.maps[operation.destination], operation.destination_offset,
                         operation.size, replacement)) return {};
        }
        return plan;
    }
    std::span<const Command> Commands() const noexcept { return {commands.data(), command_count}; }
    std::span<const Node> Nodes() const noexcept { return {nodes.data(), node_count}; }
    std::span<const Span> Spans(Index buffer) const noexcept {
        return buffer < buffer_count ? std::span<const Span>{maps[buffer].spans.data(), maps[buffer].count}
                                     : std::span<const Span>{};
    }
    bool HasRoot(Index buffer, Offset offset, Offset size, Index fill, Offset root_offset) const noexcept {
        if (buffer >= buffer_count || !size || offset >= sizes[buffer] || size > sizes[buffer] - offset ||
            fill >= command_count || commands[fill].kind != Kind::Fill ||
            root_offset >= commands[fill].size || size > commands[fill].size - root_offset) return false;
        Offset covered{};
        for (const auto& part : Spans(buffer)) {
            const auto begin = std::max(offset, part.offset);
            const auto end = std::min(offset + size, part.offset + part.size);
            if (begin >= end) continue;
            const auto root = RootOf(part.node, part.node_offset + begin - part.offset);
            if (!root || root->command != fill || root->offset != root_offset + begin - offset) return false;
            covered += end - begin;
        }
        return covered == size;
    }
    bool WrittenBy(Index command, Index buffer, Offset offset, Offset size) const noexcept {
        if (command >= command_count || !size) return false;
        const auto& operation = commands[command];
        return operation.destination == buffer && offset >= operation.destination_offset &&
            offset - operation.destination_offset < operation.size &&
            size <= operation.size - (offset - operation.destination_offset);
    }

private:
    struct Map { std::array<Span, MaxSpans> spans{}; Index count{}; };
    static bool Install(Map& current, Offset offset, Offset size, const Map& replacement) noexcept {
        Map next;
        const auto append = [&](Span part) {
            if (!part.size) return true;
            if (next.count == MaxSpans) return false;
            next.spans[next.count++] = part;
            return true;
        };
        // Preserve exact prior nodes for BOTH untouched complements.
        for (Index i = 0; i < current.count; ++i) {
            auto part = current.spans[i];
            if (part.offset < offset) {
                part.size = std::min(part.size, offset - part.offset);
                if (!append(part)) return false;
            }
        }
        for (Index i = 0; i < replacement.count; ++i)
            if (!append(replacement.spans[i])) return false;
        for (Index i = 0; i < current.count; ++i) {
            auto part = current.spans[i];
            const auto begin = std::max(part.offset, offset + size);
            if (begin < part.offset + part.size) {
                part.size -= begin - part.offset;
                part.node_offset += begin - part.offset;
                part.offset = begin;
                if (!append(part)) return false;
            }
        }
        current = next;
        return true;
    }
    std::optional<Root> RootOf(Index node, Offset offset) const noexcept {
        for (Index depth = 0; depth <= MaxCommands && node != Unknown; ++depth) {
            if (node >= node_count || offset >= nodes[node].size) return {};
            const auto& current = nodes[node];
            if (commands[current.command].kind == Kind::Fill) return Root{current.command, offset};
            offset += current.parent_offset;
            node = current.parent;
        }
        return {};
    }
    std::array<Offset, MaxBuffers> sizes{};
    std::array<Map, MaxBuffers> maps{};
    std::array<Command, MaxCommands> commands{};
    std::array<Node, MaxNodes> nodes{};
    Index buffer_count{}, command_count{}, node_count{};
};
} // namespace Vulkan
