// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include "video_core/buffer_cache/physical_write_identity.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace Vulkan {
class Scheduler;
class BufferCacheRuntime;
class PrivateProducerTestAccess;

// Only the builtin private-allocation factory can create production capabilities.
// No adoption of guest memory, external buffers, raw pointers or NCE mappings.
class PrivateProducerGuard {
    friend class Scheduler;
    friend class BufferCacheRuntime;
    friend class PrivateProducerTestAccess;
    struct State {
        static constexpr u64 Page = 4096;
        std::mutex mutex;
        void* page{};
        u64 extent{};
        std::shared_ptr<Common::GuestBackingEpoch> backing=std::make_shared<Common::GuestBackingEpoch>();
        std::shared_ptr<Common::MappingEpoch> epoch=std::make_shared<Common::MappingEpoch>();
        std::array<const void*,3> identities{};
        size_t count{};
        bool closed{};
        PrivateProducerGuard* active{}; // identity only; never dereferenced
        bool Protect(bool writable) noexcept {
#if defined(__linux__) && !defined(__ANDROID__)
            return page && mprotect(page,Page,PROT_READ|(writable?PROT_WRITE:0))==0;
#else
            (void)writable;return false;
#endif
        }
        ~State() {
#if defined(__linux__) && !defined(__ANDROID__)
            if(page) (void)munmap(page,Page);
#endif
        }
        template<class F> bool ReadPhysicalMappingSnapshot(F f) const {
            return f(epoch,epoch->generation.load(),backing,Page,[](u64 address)->std::optional<u64>{return address%Page;});
        }
        bool Live() const noexcept {
            return backing->alive.load() && epoch->alive.load() && epoch->generation.load()==0;
        }
        void CloseLocked() noexcept {
            closed=true;
            (void)Protect(false); // Active guards were already protected before admission.
            backing->alive.store(false);epoch->alive.store(false);
        }
        void Close() noexcept {std::scoped_lock lock{mutex};CloseLocked();}
    };
    static std::shared_ptr<State> Create(u64 extent) {
#if defined(__linux__) && !defined(__ANDROID__)
        if(!extent || extent>State::Page || sysconf(_SC_PAGESIZE)!=static_cast<long>(State::Page)) return {};
        auto state=std::make_shared<State>();state->extent=extent;
        state->page=mmap(nullptr,State::Page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(state->page==MAP_FAILED){state->page=nullptr;return {};}
        return state;
#else
        (void)extent;return {};
#endif
    }
    static std::shared_ptr<PrivateProducerGuard> Acquire(const std::shared_ptr<State>& state) {
        if(!state) return {};
        auto guard=std::shared_ptr<PrivateProducerGuard>(new PrivateProducerGuard(state));
        std::scoped_lock lock{state->mutex};
        if(state->closed || state->active || !state->Live() || state->count<2 || state->count>3) return {};
        if(!state->Protect(false)){state->CloseLocked();return {};}
        state->active=guard.get(); // Permission publication follows the actual OS protection.
        return guard;
    }
    explicit PrivateProducerGuard(std::shared_ptr<State> state_):state{std::move(state_)}{}
    const std::shared_ptr<State> state;
    bool done{}; // state mutex
    bool Ready() const noexcept {
        std::scoped_lock lock{state->mutex};
        return !done && !state->closed && state->active==this && state->Live();
    }
    bool Matches(const VideoCommon::PhysicalWriteIdentity& physical) const noexcept {
        if(!Ready() || physical.Backing()!=state->backing || physical.DeviceEpoch()!=state->epoch ||
           physical.NativeOffset()!=0 || physical.Size()!=state->extent || !physical.MatchesCurrent(*state)) return false;
        for(size_t i=0;i<state->count;++i) if(state->identities[i]==physical.Representation()) return true;
        return false;
    }
    // Only Scheduler may certify a checked GPU completion. Old receipts cannot
    // release a later guard: both capability identity and done state are checked.
    bool CompleteChecked() noexcept {
        std::scoped_lock lock{state->mutex};
        if(done) return true;
        if(state->active!=this) return false;
        if(state->closed || !state->Live() || !state->Protect(true)){state->CloseLocked();return false;}
        done=true;state->active=nullptr;
        return true;
    }
    void Quarantine() noexcept {
        std::scoped_lock lock{state->mutex};
        if(!done && state->active==this) state->CloseLocked();
    }
    // Synchronous private CPU store only, after physical fan-out on the owner.
    // No pointer escapes. Competing managed entry remains closed under mutex.
    bool CommitCpu(u64 offset,std::span<const u8> bytes) noexcept {
        std::scoped_lock lock{state->mutex};
        if(done || state->closed || state->active!=this || !state->Live() || bytes.empty() ||
           offset>=state->extent || bytes.size()>state->extent-offset || !state->Protect(true)) return false;
        std::memcpy(static_cast<u8*>(state->page)+offset,bytes.data(),bytes.size());
        done=true;state->active=nullptr;
        return true;
    }
public:
    ~PrivateProducerGuard() {Quarantine();} // Dropping an unfinished guard NEVER unlocks.
    PrivateProducerGuard(const PrivateProducerGuard&)=delete;
    PrivateProducerGuard& operator=(const PrivateProducerGuard&)=delete;
};
} // namespace Vulkan
