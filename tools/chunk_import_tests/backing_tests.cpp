// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include <map>
#include <mutex>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "common/host_memory.h"

struct Exporter : Common::GuestBackingExporter {
    int creates{}, releases{};
    bool fail_description{};
    int CreateChunk(size_t size) override {
        ++creates;
        int fd = memfd_create("fake-exported-chunk", MFD_CLOEXEC);
        assert(fd >= 0 && ftruncate(fd, size) == 0);
        return fd;
    }
    void ReleaseChunk(int fd) noexcept override {
        ++releases;
        assert(close(fd) == 0);
    }
    std::optional<Common::GuestChunkExportInfo> DescribeChunk(int) override {
        if (fail_description)
            throw std::bad_alloc{};
        Common::GuestChunkExportInfo info;
        info.allocation_size = 32ULL << 20;
        return info; // fake export, not a real Vulkan dma-buf integration
    }
};
using DAddr = u64;
struct Mapper {
    std::mutex mapping_guard;
    const Common::HostMemory* guest_backing;
    u64 device_as_size = 1ULL << 32;
    std::map<u64, u64> pages;
    template <class T>
    const T* GetPointer(u64 d) const {
        auto it = pages.find(d);
        return it == pages.end()
                   ? nullptr
                   : reinterpret_cast<const T*>(guest_backing->BackingBasePointer() + it->second);
    }
    u64 GetPhysicalRawAddressFromDAddr(u64 d) const {
        return pages.at(d);
    }
    /* PRODUCTION_QUERY */
};
int main() {
    constexpr u64 chunk = 32ULL << 20;
    auto exporter = std::make_shared<Exporter>();
    Common::GuestChunkLease held;
    u64 generation = 0;
    int fd = -1;
    {
        Common::HostMemory memory(4 * chunk, 16ULL << 20, {}, chunk, false, true, exporter);
        generation = memory.BackingGeneration();
        assert(!memory.GetPublishedChunk(0) && !memory.FirstPublishedExportedChunk());
        assert(memory.GetBackingStats().chunks_created == 0 && exporter->creates == 0);
        memory.EnsureBackingRange(0, 4096);
        held = memory.GetPublishedChunk(0);
        assert(held && held->Alive() && held->exported && held->epoch->generation == generation);
        assert(held->physical_offset == 0 && held->size == chunk && held->export_info);
        fd = held->fd;
        Mapper mapper{{}, &memory, 1ULL << 32, {{64, 0}, {128, 0}, {192, chunk}}};
        const std::map<u64, u64> gpu_aliases{{0x1000, 64}, {0x9000, 128}};
        auto a = mapper.GetPublishedGuestChunk(gpu_aliases.at(0x1000));
        auto b = mapper.GetPublishedGuestChunk(gpu_aliases.at(0x9000));
        assert(a->index == b->index && a->epoch == b->epoch && a->fd == b->fd);
        assert(!mapper.GetPublishedGuestChunk(192));
        assert(exporter->creates == 1 && memory.GetBackingStats().chunks_created == 1);
        assert(!memory.GetPublishedChunk(4 * chunk));
        std::cout << "PASS production publication/query: cold lookup does not allocate, DAddr "
                     "aliases resolve same physical chunk, immutable descriptor\n";
    }
    assert(!held->Alive() && fcntl(fd, F_GETFD) >= 0 && exporter->releases == 0);
    // Lease pins fd and exporter until the imported resource's last reference is gone.
    held.reset();
    assert(exporter->releases == 1 && fcntl(fd, F_GETFD) == -1);
    {
        Common::HostMemory replacement(4 * chunk, 16ULL << 20, {}, chunk, false, true, exporter);
        assert(replacement.BackingGeneration() != generation);
        replacement.EnsureBackingRange(chunk, 4096);
        auto item = replacement.GetPublishedChunk(chunk);
        assert(item->index == 1 && item->physical_offset == chunk);
        Common::HostMemory moved(std::move(replacement));
        assert(item->Alive() && moved.BackingGeneration() == item->epoch->generation);
        Common::HostMemory target(4 * chunk, 16ULL << 20, {}, chunk, false, true);
        target.EnsureBackingRange(0, 1);
        auto old = target.GetPublishedChunk(0);
        target = std::move(moved);
        assert(!old->Alive() && item->Alive());
    }
    assert(exporter->releases == 2);
    std::cout << "PASS production backing lifetime: invalidation on destruction/replacement, fd "
                 "pinned until lease release, moves transfer arena generation\n";
    exporter->fail_description = true;
    {
        Common::HostMemory memory(4 * chunk, 16ULL << 20, {}, chunk, false, true, exporter);
        memory.EnsureBackingRange(0, 1);
        auto published = memory.GetPublishedChunk(0);
        assert(published && published->exported && !published->export_info);
        assert(!memory.FirstPublishedExportedChunk());
        assert(memory.GetBackingStats().chunks_created == 1 &&
               memory.GetBackingStats().chunks_exported == 1);
        memory.BackingBasePointer()[0] = 77;
        assert(memory.BackingBasePointer()[0] == 77);
    }
    assert(exporter->releases == 3);
    std::cout << "PASS optional metadata failure: CPU-ready exported backing stays published and "
                 "writable; no diagnostic candidate\n";
}
