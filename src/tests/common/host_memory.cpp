// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/host_memory.h"
#include "common/literals.h"

using Common::HostMemory;
using namespace Common::Literals;

static constexpr size_t VIRTUAL_SIZE = 1ULL << 39;
static constexpr size_t BACKING_SIZE = 4_GiB;
static constexpr auto PERMS = Common::MemoryPermission::ReadWrite;
static constexpr auto HEAP = false;

TEST_CASE("HostMemory: Initialize and deinitialize", "[common]") {
    { HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE); }
    { HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE); }
}

TEST_CASE("HostMemory: Simple map", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x5000, 0x8000, 0x1000, PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + 0x5000;
    data[0] = 50;
    REQUIRE(data[0] == 50);
}

TEST_CASE("HostMemory: Simple mirror map", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x5000, 0x3000, 0x2000, PERMS, HEAP);
    mem.Map(0x8000, 0x4000, 0x1000, PERMS, HEAP);

    volatile u8* const mirror_a = mem.VirtualBasePointer() + 0x5000;
    volatile u8* const mirror_b = mem.VirtualBasePointer() + 0x8000;
    mirror_b[0] = 76;
    REQUIRE(mirror_a[0x1000] == 76);
}

TEST_CASE("HostMemory: Simple unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x5000, 0x3000, 0x2000, PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + 0x5000;
    data[75] = 50;
    REQUIRE(data[75] == 50);

    mem.Unmap(0x5000, 0x2000, HEAP);
}

TEST_CASE("HostMemory: Simple unmap and remap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x5000, 0x3000, 0x2000, PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + 0x5000;
    data[0] = 50;
    REQUIRE(data[0] == 50);

    mem.Unmap(0x5000, 0x2000, HEAP);

    mem.Map(0x5000, 0x3000, 0x2000, PERMS, HEAP);
    REQUIRE(data[0] == 50);

    mem.Map(0x7000, 0x2000, 0x5000, PERMS, HEAP);
    REQUIRE(data[0x3000] == 50);
}

TEST_CASE("HostMemory: Nieche allocation", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x0000, 0, 0x20000, PERMS, HEAP);
    mem.Unmap(0x0000, 0x4000, HEAP);
    mem.Map(0x1000, 0, 0x2000, PERMS, HEAP);
    mem.Map(0x3000, 0, 0x1000, PERMS, HEAP);
    mem.Map(0, 0, 0x1000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Full unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x8000, 0x4000, HEAP);
    mem.Map(0x6000, 0, 0x16000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Right out of bounds unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x0000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x2000, 0x4000, HEAP);
    mem.Map(0x2000, 0x80000, 0x4000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Left out of bounds unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x6000, 0x4000, HEAP);
    mem.Map(0x8000, 0, 0x2000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Multiple placeholder unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x0000, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x4000, 0, 0x1b000, PERMS, HEAP);
    mem.Unmap(0x3000, 0x1c000, HEAP);
    mem.Map(0x3000, 0, 0x20000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap between placeholders", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x0000, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x4000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x2000, 0x4000, HEAP);
    mem.Map(0x2000, 0, 0x4000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap to origin", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x4000, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x4000, 0x4000, HEAP);
    mem.Map(0, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x4000, 0, 0x4000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap to right", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x4000, 0, 0x4000, PERMS, HEAP);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
    mem.Unmap(0x8000, 0x4000, HEAP);
    mem.Map(0x8000, 0, 0x4000, PERMS, HEAP);
}

TEST_CASE("HostMemory: Partial right unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x4000, 0x10000, 0x4000, PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + 0x4000;
    ptr[0x1000] = 17;

    mem.Unmap(0x6000, 0x2000, HEAP);

    REQUIRE(ptr[0x1000] == 17);
}

TEST_CASE("HostMemory: Partial left unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x4000, 0x10000, 0x4000, PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + 0x4000;
    ptr[0x3000] = 19;
    ptr[0x3fff] = 12;

    mem.Unmap(0x4000, 0x2000, HEAP);

    REQUIRE(ptr[0x3000] == 19);
    REQUIRE(ptr[0x3fff] == 12);
}

TEST_CASE("HostMemory: Partial middle unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x4000, 0x10000, 0x4000, PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + 0x4000;
    ptr[0x0000] = 19;
    ptr[0x3fff] = 12;

    mem.Unmap(0x1000, 0x2000, HEAP);

    REQUIRE(ptr[0x0000] == 19);
    REQUIRE(ptr[0x3fff] == 12);
}

TEST_CASE("HostMemory: Partial sparse middle unmap and check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    mem.Map(0x4000, 0x10000, 0x2000, PERMS, HEAP);
    mem.Map(0x6000, 0x20000, 0x2000, PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + 0x4000;
    ptr[0x0000] = 19;
    ptr[0x3fff] = 12;

    mem.Unmap(0x5000, 0x2000, HEAP);

    REQUIRE(ptr[0x0000] == 19);
    REQUIRE(ptr[0x3fff] == 12);
}

// A backing made of several file descriptors, as when guest memory is exported from the GPU
// driver in chunks. Plain memfds stand in for the exported memory.
namespace {

constexpr bool SameRange(Common::BackingChunkRange a, Common::BackingChunkRange b) {
    return a.chunk == b.chunk && a.offset == b.offset && a.length == b.length;
}
using Common::FirstChunkRange;
static_assert(SameRange(FirstChunkRange(0, 0x1000, 0x10000), {0, 0, 0x1000}));
// Ends exactly at the end of chunk 2: still one piece.
static_assert(SameRange(FirstChunkRange(0x2F000, 0x1000, 0x10000), {2, 0xF000, 0x1000}));
// Crosses into chunk 1: the first piece stops at the boundary, the rest starts chunk 1.
static_assert(SameRange(FirstChunkRange(0xF000, 0x3000, 0x10000), {0, 0xF000, 0x1000}));
static_assert(SameRange(FirstChunkRange(0x10000, 0x2000, 0x10000), {1, 0, 0x2000}));
// A single file descriptor of the whole size never splits.
static_assert(SameRange(FirstChunkRange(0xFFFFF000, 0x1000, 4_GiB), {0, 0xFFFFF000, 0x1000}));

} // Anonymous namespace

#ifdef __linux__
#include <array>
#include <sys/mman.h>
#include <unistd.h>

namespace {

constexpr size_t CHUNK_SIZE = 0x10000;
constexpr size_t CHUNK_COUNT = 4;

// `fds` go to HostMemory, which closes them. `views` map each chunk on its own, to check which
// file descriptor a byte really landed in.
struct Chunks {
    std::array<int, CHUNK_COUNT> fds;
    std::array<volatile u8*, CHUNK_COUNT> views;
};

Chunks MakeChunks() {
    Chunks chunks{};
    for (size_t i = 0; i < CHUNK_COUNT; ++i) {
        chunks.fds[i] = memfd_create("HostMemoryTestChunk", 0);
        REQUIRE(chunks.fds[i] >= 0);
        REQUIRE(ftruncate(chunks.fds[i], CHUNK_SIZE) == 0);
        void* const view =
            mmap(nullptr, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, chunks.fds[i], 0);
        REQUIRE(view != MAP_FAILED);
        chunks.views[i] = static_cast<volatile u8*>(view);
    }
    return chunks;
}

void FreeViews(const Chunks& chunks) {
    for (volatile u8* const view : chunks.views) {
        munmap(const_cast<u8*>(view), CHUNK_SIZE);
    }
}

} // Anonymous namespace

TEST_CASE("HostMemory: Chunked backing is one contiguous view", "[common]") {
    const Chunks chunks = MakeChunks();
    {
        HostMemory mem(CHUNK_SIZE * CHUNK_COUNT, VIRTUAL_SIZE, chunks.fds, CHUNK_SIZE);
        volatile u8* const backing = mem.BackingBasePointer();
        for (size_t i = 0; i < CHUNK_COUNT; ++i) {
            backing[i * CHUNK_SIZE] = static_cast<u8>(10 + i);
            backing[(i + 1) * CHUNK_SIZE - 1] = static_cast<u8>(20 + i);
        }
        for (size_t i = 0; i < CHUNK_COUNT; ++i) {
            REQUIRE(chunks.views[i][0] == static_cast<u8>(10 + i));
            REQUIRE(chunks.views[i][CHUNK_SIZE - 1] == static_cast<u8>(20 + i));
        }
    }
    FreeViews(chunks);
}

TEST_CASE("HostMemory: Map across a chunk boundary", "[common]") {
    const Chunks chunks = MakeChunks();
    {
        HostMemory mem(CHUNK_SIZE * CHUNK_COUNT, VIRTUAL_SIZE, chunks.fds, CHUNK_SIZE);
        // Two pages at the end of chunk 0 and two at the start of chunk 1.
        mem.Map(0x5000, 0xE000, 0x4000, PERMS, HEAP);
        volatile u8* const data = mem.VirtualBasePointer() + 0x5000;
        for (size_t i = 0; i < 0x4000; i += 0x800) {
            data[i] = static_cast<u8>(1 + i / 0x800);
        }
        volatile u8* const backing = mem.BackingBasePointer();
        for (size_t i = 0; i < 0x4000; i += 0x800) {
            REQUIRE(backing[0xE000 + i] == static_cast<u8>(1 + i / 0x800));
        }
        REQUIRE(chunks.views[0][0xF800] == 4);
        REQUIRE(chunks.views[1][0x0000] == 5);
        REQUIRE(chunks.views[1][0x1800] == 8);

        // The reserve and a second mapping still work across the boundary.
        mem.Unmap(0x5000, 0x4000, HEAP);
        chunks.views[1][0x10] = 99;
        mem.Map(0x5000, 0xE000, 0x4000, PERMS, HEAP);
        REQUIRE(data[0x2010] == 99);
    }
    FreeViews(chunks);
}

TEST_CASE("HostMemory: Map across three chunks", "[common]") {
    const Chunks chunks = MakeChunks();
    {
        HostMemory mem(CHUNK_SIZE * CHUNK_COUNT, VIRTUAL_SIZE, chunks.fds, CHUNK_SIZE);
        // The last page of chunk 0, all of chunk 1 and the first page of chunk 2.
        mem.Map(0x20000, 0xF000, 0x12000, PERMS, HEAP);
        volatile u8* const data = mem.VirtualBasePointer() + 0x20000;
        data[0x0000] = 31;
        data[0x1000] = 32;
        data[0x10FFF] = 33;
        data[0x11000] = 34;
        REQUIRE(chunks.views[0][0xF000] == 31);
        REQUIRE(chunks.views[1][0x0000] == 32);
        REQUIRE(chunks.views[1][0xFFFF] == 33);
        REQUIRE(chunks.views[2][0x0000] == 34);
    }
    FreeViews(chunks);
}
#endif
