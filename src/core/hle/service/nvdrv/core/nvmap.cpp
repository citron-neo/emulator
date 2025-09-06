// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: 2022 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/heap_mapper.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/memory.h"
#include "video_core/host1x/host1x.h"

using Core::Memory::CITRON_PAGESIZE;
constexpr size_t BIG_PAGE_SIZE = CITRON_PAGESIZE * 16;

namespace Service::Nvidia::NvCore {
NvMap::Handle::Handle(u64 size_, Id id_)
    : size(size_), aligned_size(size), orig_size(size), id(id_) {
    flags.raw = 0;
}

NvResult NvMap::Handle::Alloc(Flags pFlags, u32 pAlign, u8 pKind, u64 pAddress,
                              NvCore::SessionId pSessionId) {
    std::scoped_lock lock(mutex);
    // Handles cannot be allocated twice
    if (allocated) {
        return NvResult::AccessDenied;
    }

    flags = pFlags;
    kind = pKind;
    align = pAlign < CITRON_PAGESIZE ? CITRON_PAGESIZE : pAlign;
    session_id = pSessionId;

    // This flag is only applicable for handles with an address passed
    if (pAddress) {
        flags.keep_uncached_after_free.Assign(0);
    } else {
        LOG_CRITICAL(Service_NVDRV,
                     "Mapping nvmap handles without a CPU side address is unimplemented!");
    }

    size = Common::AlignUp(size, CITRON_PAGESIZE);
    aligned_size = Common::AlignUp(size, align);
    address = pAddress;
    allocated = true;

    return NvResult::Success;
}

NvResult NvMap::Handle::Duplicate(bool internal_session) {
    std::scoped_lock lock(mutex);
    // Unallocated handles cannot be duplicated as duplication requires memory accounting (in HOS)
    if (!allocated) [[unlikely]] {
        return NvResult::BadValue;
    }

    // If we internally use FromId the duplication tracking of handles won't work accurately due to
    // us not implementing per-process handle refs.
    if (internal_session) {
        internal_dupes++;
    } else {
        dupes++;
    }

    return NvResult::Success;
}

NvMap::NvMap(Container& core_, Tegra::Host1x::Host1x& host1x_) : host1x{host1x_}, core{core_} {}

void NvMap::AddHandle(std::shared_ptr<Handle> handle_description) {
    std::scoped_lock lock(handles_lock);

    handles.emplace(handle_description->id, std::move(handle_description));
}

void NvMap::UnmapHandle(Handle& handle_description) {
    // Remove pending unmap queue entry if needed
    if (handle_description.unmap_queue_entry) {
        unmap_queue.erase(*handle_description.unmap_queue_entry);
        handle_description.unmap_queue_entry.reset();
    }

    // Free and unmap the handle from Host1x GMMU
    if (handle_description.pin_virt_address) {
        host1x.GMMU().Unmap(static_cast<GPUVAddr>(handle_description.pin_virt_address),
                            handle_description.aligned_size);
        host1x.Allocator().Free(handle_description.pin_virt_address,
                                static_cast<u32>(handle_description.aligned_size));
        handle_description.pin_virt_address = 0;
    }

    // Free and unmap the handle from the SMMU
    const size_t map_size = handle_description.aligned_size;
    if (!handle_description.in_heap) {
        auto& smmu = host1x.MemoryManager();
        size_t aligned_up = Common::AlignUp(map_size, BIG_PAGE_SIZE);
        smmu.Unmap(handle_description.d_address, map_size);
        smmu.Free(handle_description.d_address, static_cast<size_t>(aligned_up));
        handle_description.d_address = 0;
        return;
    }
    const VAddr vaddress = handle_description.address;
    auto* session = core.GetSession(handle_description.session_id);
    session->mapper->Unmap(vaddress, map_size);
    handle_description.d_address = 0;
    handle_description.in_heap = false;
}

bool NvMap::TryRemoveHandle(const Handle& handle_description) {
    // No dupes left, we can remove from handle map
    if (handle_description.dupes == 0 && handle_description.internal_dupes == 0) {
        std::scoped_lock lock(handles_lock);

        auto it{handles.find(handle_description.id)};
        if (it != handles.end()) {
            handles.erase(it);
        }

        return true;
    } else {
        return false;
    }
}

NvResult NvMap::CreateHandle(u64 size, std::shared_ptr<NvMap::Handle>& result_out) {
    if (!size) [[unlikely]] {
        return NvResult::BadValue;
    }

    u32 id{next_handle_id.fetch_add(HandleIdIncrement, std::memory_order_relaxed)};
    auto handle_description{std::make_shared<Handle>(size, id)};
    AddHandle(handle_description);

    result_out = handle_description;
    return NvResult::Success;
}

std::shared_ptr<NvMap::Handle> NvMap::GetHandle(Handle::Id handle) {
    std::scoped_lock lock(handles_lock);
    try {
        return handles.at(handle);
    } catch (std::out_of_range&) {
        return nullptr;
    }
}

DAddr NvMap::GetHandleAddress(Handle::Id handle) {
    std::scoped_lock lock(handles_lock);
    try {
        return handles.at(handle)->d_address;
    } catch (std::out_of_range&) {
        return 0;
    }
}

DAddr NvMap::PinHandle(NvMap::Handle::Id handle, bool low_area_pin) {
    auto handle_description{GetHandle(handle)};
    if (!handle_description) [[unlikely]] {
        return 0;
    }

    std::scoped_lock lock(handle_description->mutex);
    const auto map_low_area = [&] {
        if (handle_description->pin_virt_address == 0) {
            auto& gmmu_allocator = host1x.Allocator();
            auto& gmmu = host1x.GMMU();
            u32 address =
                gmmu_allocator.Allocate(static_cast<u32>(handle_description->aligned_size));
            gmmu.Map(static_cast<GPUVAddr>(address), handle_description->d_address,
                     handle_description->aligned_size);
            handle_description->pin_virt_address = address;
        }
    };
    if (!handle_description->pins) {
        // If we're in the unmap queue we can just remove ourselves and return since we're already
        // mapped
        {
            // Lock now to prevent our queue entry from being removed for allocation in-between the
            // following check and erase
            std::scoped_lock queueLock(unmap_queue_lock);
            if (handle_description->unmap_queue_entry) {
                unmap_queue.erase(*handle_description->unmap_queue_entry);
                handle_description->unmap_queue_entry.reset();

                if (low_area_pin) {
                    map_low_area();
                    handle_description->pins++;
                    return static_cast<DAddr>(handle_description->pin_virt_address);
                }

                handle_description->pins++;
                return handle_description->d_address;
            }
        }

        using namespace std::placeholders;
        // If not then allocate some space and map it
        DAddr address{};
        auto& smmu = host1x.MemoryManager();
        auto* session = core.GetSession(handle_description->session_id);
        const VAddr vaddress = handle_description->address;
        const size_t map_size = handle_description->aligned_size;
        if (session->has_preallocated_area && session->mapper->IsInBounds(vaddress, map_size)) {
            handle_description->d_address = session->mapper->Map(vaddress, map_size);
            handle_description->in_heap = true;
        } else {
            size_t aligned_up = Common::AlignUp(map_size, BIG_PAGE_SIZE);
            while ((address = smmu.Allocate(aligned_up)) == 0) {
                // Free handles until the allocation succeeds
                std::scoped_lock queueLock(unmap_queue_lock);
                if (auto freeHandleDesc{unmap_queue.front()}) {
                    // Handles in the unmap queue are guaranteed not to be pinned so don't bother
                    // checking if they are before unmapping
                    std::scoped_lock freeLock(freeHandleDesc->mutex);
                    if (handle_description->d_address)
                        UnmapHandle(*freeHandleDesc);
                } else {
                    LOG_CRITICAL(Service_NVDRV, "Ran out of SMMU address space!");
                }
            }

            handle_description->d_address = address;
            smmu.Map(address, vaddress, map_size, session->asid, true);
            handle_description->in_heap = false;
        }
    }

    if (low_area_pin) {
        map_low_area();
    }

    handle_description->pins++;
    if (low_area_pin) {
        return static_cast<DAddr>(handle_description->pin_virt_address);
    }
    return handle_description->d_address;
}

void NvMap::UnpinHandle(Handle::Id handle) {
    auto handle_description{GetHandle(handle)};
    if (!handle_description) {
        return;
    }

    std::scoped_lock lock(handle_description->mutex);
    if (--handle_description->pins < 0) {
        LOG_WARNING(Service_NVDRV, "Pin count imbalance detected!");
    } else if (!handle_description->pins) {
        std::scoped_lock queueLock(unmap_queue_lock);

        // Add to the unmap queue allowing this handle's memory to be freed if needed
        unmap_queue.push_back(handle_description);
        handle_description->unmap_queue_entry = std::prev(unmap_queue.end());
    }
}

void NvMap::DuplicateHandle(Handle::Id handle, bool internal_session) {
    auto handle_description{GetHandle(handle)};
    if (!handle_description) {
        LOG_CRITICAL(Service_NVDRV, "Unregistered handle!");
        return;
    }

    auto result = handle_description->Duplicate(internal_session);
    if (result != NvResult::Success) {
        LOG_CRITICAL(Service_NVDRV, "Could not duplicate handle!");
    }
}

std::optional<NvMap::FreeInfo> NvMap::FreeHandle(Handle::Id handle, bool internal_session) {
    std::weak_ptr<Handle> hWeak{GetHandle(handle)};
    FreeInfo freeInfo;

    // We use a weak ptr here so we can tell when the handle has been freed and report that back to
    // guest
    if (auto handle_description = hWeak.lock()) {
        std::scoped_lock lock(handle_description->mutex);

        if (internal_session) {
            if (--handle_description->internal_dupes < 0)
                LOG_WARNING(Service_NVDRV, "Internal duplicate count imbalance detected!");
        } else {
            if (--handle_description->dupes < 0) {
                LOG_WARNING(Service_NVDRV, "User duplicate count imbalance detected!");
            } else if (handle_description->dupes == 0) {
                // Force unmap the handle
                if (handle_description->d_address) {
                    std::scoped_lock queueLock(unmap_queue_lock);
                    UnmapHandle(*handle_description);
                }

                handle_description->pins = 0;
            }
        }

        // Try to remove the shared ptr to the handle from the map, if nothing else is using the
        // handle then it will now be freed when `handle_description` goes out of scope
        if (TryRemoveHandle(*handle_description)) {
            LOG_DEBUG(Service_NVDRV, "Removed nvmap handle: {}", handle);
        } else {
            LOG_DEBUG(Service_NVDRV,
                      "Tried to free nvmap handle: {} but didn't as it still has duplicates",
                      handle);
        }

        freeInfo = {
            .address = handle_description->address,
            .size = handle_description->size,
            .was_uncached = handle_description->flags.map_uncached.Value() != 0,
            .can_unlock = true,
        };
    } else {
        return std::nullopt;
    }

    // If the handle hasn't been freed from memory, mark that
    if (!hWeak.expired()) {
        LOG_DEBUG(Service_NVDRV, "nvmap handle: {} wasn't freed as it is still in use", handle);
        freeInfo.can_unlock = false;
    }

    return freeInfo;
}

void NvMap::UnmapAllHandles(NvCore::SessionId session_id) {
    auto handles_copy = [&] {
        std::scoped_lock lk{handles_lock};
        return handles;
    }();

    for (auto& [id, handle] : handles_copy) {
        {
            std::scoped_lock lk{handle->mutex};
            if (handle->session_id.id != session_id.id || handle->dupes <= 0) {
                continue;
            }
        }
        FreeHandle(id, false);
    }
}

} // namespace Service::Nvidia::NvCore
