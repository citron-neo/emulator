// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <latch>
#include <new>
#include <thread>
#include <vector>
#include "video_core/renderer_vulkan/guest_chunk_import_cache.h"
using namespace Vulkan;
std::atomic<bool> fail_next_new{};
void* operator new(std::size_t size) {
    if (fail_next_new.exchange(false))
        throw std::bad_alloc{};
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

struct Backend {
    struct Resource {
        ChunkImportObservation info;
        Common::GuestChunkLease lease;
        std::atomic<unsigned>* deaths;
        ~Resource() {
            ++*deaths;
        }
    };
    struct Result {
        std::unique_ptr<Resource> resource;
        ChunkImportRejection reason{ChunkImportRejection::None};
        s32 vk_result{};
    };
    std::atomic<unsigned> creates{}, deaths{};
    bool block{}, fail{}, fail_publish{};
    std::latch entered{1}, resume{1};
    Result Create(Common::GuestChunkLease chunk) {
        ++creates;
        if (block) {
            entered.count_down();
            resume.wait();
        }
        if (fail)
            return {nullptr, ChunkImportRejection::AllocateMemory, -2};
        auto resource = std::make_unique<Resource>();
        resource->info = {chunk->epoch->generation, chunk->index, chunk->size, 4096, 1, 0};
        resource->lease = chunk;
        resource->deaths = &deaths;
        if (fail_publish)
            fail_next_new = true;
        return {std::move(resource)};
    }
};
Common::GuestChunkLease lease(const Common::GuestBackingLifetime& lifetime, u64 index = 0) {
    return std::make_shared<const Common::PublishedGuestChunk>(
        Common::PublishedGuestChunk{std::make_shared<int>(0),
                                    lifetime.Get(),
                                    index,
                                    index * (32ULL << 20),
                                    32ULL << 20,
                                    10,
                                    true,
                                    {}});
}
int main() {
    Common::GuestBackingLifetime lifetime;
    auto chunk = lease(lifetime);
    Backend b;
    GuestChunkImportCache<Backend> cache{b};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 16; ++i)
        workers.emplace_back([&] { assert(cache.Request(chunk)); });
    for (auto& t : workers)
        t.join();
    assert(b.creates == 1 && cache.GetStats().import_cache_hits == 15);
    assert(cache.Request(lease(lifetime, 1)) && b.creates == 2);
    assert(cache.GetStats().active_imports == 2 &&
           cache.GetStats().imported_logical_bytes == (64ULL << 20));
    Common::GuestBackingLifetime reboot;
    assert(reboot.Get()->generation != lifetime.Get()->generation);
    assert(cache.Request(lease(reboot)) && cache.GetStats().cache_entries_released == 2);
    assert(cache.GetStats().active_imports == 1);
    const auto epoch = cache.GetStats().device_epoch;
    cache.Close();
    assert(cache.GetStats().active_imports == 0 && !cache.Request(chunk));
    GuestChunkImportCache<Backend> recreated{b};
    assert(recreated.GetStats().device_epoch != epoch && recreated.Request(chunk));
    std::cout << "PASS production cache: miss/hit, concurrent dedup, distinct chunks, backing "
                 "generation and device recreation\n";

    Backend failing;
    failing.fail = true;
    GuestChunkImportCache<Backend> negative{failing};
    assert(!negative.Request(chunk) && !negative.Request(chunk));
    assert(failing.creates == 1 && negative.GetStats().import_failures == 1);
    assert(
        negative.GetStats().rejections[static_cast<size_t>(ChunkImportRejection::AllocateMemory)] ==
        2);
    assert(negative.GetStats().last_vk_result == -2);
    std::cout
        << "PASS failure cache: safe rejection, no repeated driver imports, reason counters\n";

    Backend slow;
    slow.block = true;
    GuestChunkImportCache<Backend> closing{slow};
    std::thread importer([&] { assert(!closing.Request(chunk)); });
    slow.entered.wait();
    std::thread teardown([&] { closing.Close(); });
    while (!closing.IsClosed())
        std::this_thread::yield();
    slow.resume.count_down();
    importer.join();
    teardown.join();
    assert(closing.GetStats().imports_created == 0 && closing.GetStats().active_imports == 0);
    std::cout << "PASS teardown during import: in-flight resource destroyed, no late publication\n";

    Backend replacing;
    replacing.block = true;
    GuestChunkImportCache<Backend> stale{replacing};
    auto old_owner = std::make_unique<Common::GuestBackingLifetime>();
    auto old = lease(*old_owner);
    std::thread request([&] { assert(!stale.Request(old)); });
    replacing.entered.wait();
    old_owner.reset();
    replacing.resume.count_down();
    request.join();
    assert(!old->Alive() && stale.GetStats().imports_created == 0);
    assert(!stale.Request(old));
    std::cout << "PASS guest destruction during import: stale epoch rejected without publication\n";

    Backend published;
    GuestChunkImportCache<Backend> expired{published};
    auto backing = std::make_unique<Common::GuestBackingLifetime>();
    assert(expired.Request(lease(*backing)));
    backing.reset();
    assert(expired.GetStats().active_imports == 0 &&
           expired.GetStats().cache_entries_released == 1);
    std::cout << "PASS snapshot after backing destruction: cached import invalidated and released "
                 "without guest bindings\n";

    Backend oom;
    oom.fail_publish = true;
    GuestChunkImportCache<Backend> unpublishable{oom};
    assert(!unpublishable.Request(chunk) && oom.deaths == 1);
    assert(unpublishable.GetStats().active_imports == 0 &&
           unpublishable.GetStats().import_failures == 1);
    assert(!unpublishable.Request(chunk) && oom.creates == 1);
    std::cout << "PASS publication allocation failure: partial resource destroyed, no entry "
                 "exposed, repeated import suppressed\n";
}
