// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/host_memory.h"
#include "common/literals.h"

using Common::HostMemory;
using namespace Common::Literals;

// On arm64 ChooseVirtualBase places the arena between 2^36 and 2^39 minus its size, so a 2^39
// arena never fits there and every test failed with bad_alloc on the device. 2^38 fits.
static constexpr size_t VIRTUAL_SIZE = 1ULL << 38;
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

// HostMemory merges mappings of a dma_buf backing by hand, from /proc/self/maps (the kernel
// never merges them). The rule, and the parser fed with lines as the Thor printed them.
namespace {

using Common::ContinuesMapping;
using Common::HostMapping;
using Common::MemoryPermission;

constexpr HostMapping MapsPage(u64 start, u64 offset, MemoryPermission perms = PERMS,
                               u64 inode = 4600) {
    return {start, start + 0x1000, offset, 9, inode, perms, true};
}
// Neighbours in memory and in the file, same file and permissions: one mapping.
static_assert(ContinuesMapping(MapsPage(0x7000, 0x3000), MapsPage(0x8000, 0x4000)));
// A gap in memory or in the file keeps them apart.
static_assert(!ContinuesMapping(MapsPage(0x7000, 0x3000), MapsPage(0x9000, 0x4000)));
static_assert(!ContinuesMapping(MapsPage(0x7000, 0x3000), MapsPage(0x8000, 0x5000)));
// So do different permissions (a memtrap) and a different file.
static_assert(!ContinuesMapping(MapsPage(0x7000, 0x3000),
                                MapsPage(0x8000, 0x4000, MemoryPermission::Read)));
static_assert(!ContinuesMapping(MapsPage(0x7000, 0x3000), MapsPage(0x8000, 0x4000, PERMS, 4601)));
// Anonymous memory (inode 0) is never merged by hand.
static_assert(
    !ContinuesMapping(MapsPage(0x7000, 0x3000, PERMS, 0), MapsPage(0x8000, 0x4000, PERMS, 0)));

} // Anonymous namespace

TEST_CASE("HostMemory: Parse /proc/self/maps lines", "[common]") {
    HostMapping m{};
    REQUIRE(Common::ParseHostMapping("7387f04000-7387f05000 rw-s 00000000 00:09 4600          "
                                     "                 /dmabuf:\n",
                                     m));
    REQUIRE(m.start == 0x7387f04000);
    REQUIRE(m.end == 0x7387f05000);
    REQUIRE(m.offset == 0);
    REQUIRE(m.device == 9);
    REQUIRE(m.inode == 4600);
    REQUIRE(m.perms == MemoryPermission::ReadWrite);
    REQUIRE(m.shared);

    REQUIRE(Common::ParseHostMapping("738d844000-7391844000 rw-s 00001000 00:01 6144          "
                                     "                 /memfd:vma_probe (deleted)",
                                     m));
    REQUIRE(m.offset == 0x1000);
    REQUIRE(m.device == 1);
    REQUIRE(m.inode == 6144);

    // Private, executable, a device with a major number, no path.
    REQUIRE(Common::ParseHostMapping("5f0000-5f1000 r-xp 0002a000 fd:05 123", m));
    REQUIRE(m.perms == (MemoryPermission::Read | MemoryPermission::Execute));
    REQUIRE_FALSE(m.shared);
    REQUIRE(m.device == ((u64{0xfd} << 32) | 5));
    REQUIRE(m.inode == 123);
    REQUIRE(m.offset == 0x2a000);

    // Anonymous, no access.
    REQUIRE(Common::ParseHostMapping("7ffd0000-7ffd1000 ---p 00000000 00:00 0 ", m));
    REQUIRE(m.inode == 0);
    REQUIRE(m.perms == MemoryPermission{});

    // Not maps lines.
    REQUIRE_FALSE(Common::ParseHostMapping("", m));
    REQUIRE_FALSE(Common::ParseHostMapping("hello", m));
    REQUIRE_FALSE(Common::ParseHostMapping("7000-8000 rw-s", m));
    REQUIRE_FALSE(Common::ParseHostMapping("7000-8000 rw-s 0 00:09", m));
}
