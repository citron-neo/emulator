// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <bit>
#include <deque>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "common/alignment.h"
#include "common/common_types.h"
#include "video_core/buffer_cache/word_manager.h"

namespace VideoCommon {

template <typename DeviceTracker>
class MemoryTrackerBase {
    static constexpr size_t MAX_CPU_PAGE_BITS = 34;
    static constexpr size_t HIGHER_PAGE_BITS = 22;
    static constexpr size_t HIGHER_PAGE_SIZE = 1ULL << HIGHER_PAGE_BITS;
    static constexpr size_t HIGHER_PAGE_MASK = HIGHER_PAGE_SIZE - 1ULL;
    static constexpr size_t NUM_HIGH_PAGES = 1ULL << (MAX_CPU_PAGE_BITS - HIGHER_PAGE_BITS);
    static constexpr size_t MANAGER_POOL_SIZE = 32;
    static constexpr size_t WORDS_STACK_NEEDED = HIGHER_PAGE_SIZE / BYTES_PER_WORD;
    using Manager = WordManager<DeviceTracker, WORDS_STACK_NEEDED>;

public:
    MemoryTrackerBase(DeviceTracker& device_tracker_) : device_tracker{&device_tracker_} {}
    ~MemoryTrackerBase() = default;

    /// Returns the inclusive CPU modified range in a begin end pair
    [[nodiscard]] std::pair<u64, u64> ModifiedCpuRegion(VAddr query_cpu_addr,
                                                        u64 query_size) noexcept {
        return IteratePairs<true>(
            query_cpu_addr, query_size, [](Manager* manager, u64 offset, size_t size) {
                return manager->template ModifiedRegion<Type::CPU>(offset, size);
            });
    }

    /// Returns the inclusive GPU modified range in a begin end pair
    [[nodiscard]] std::pair<u64, u64> ModifiedGpuRegion(VAddr query_cpu_addr,
                                                        u64 query_size) noexcept {
        return IteratePairs<false>(
            query_cpu_addr, query_size, [](Manager* manager, u64 offset, size_t size) {
                return manager->template ModifiedRegion<Type::GPU>(offset, size);
            });
    }

    /// Returns true if a region has been modified from the CPU
    [[nodiscard]] bool IsRegionCpuModified(VAddr query_cpu_addr, u64 query_size) noexcept {
        return IteratePages<true>(
            query_cpu_addr, query_size, [](Manager* manager, u64 offset, size_t size) {
                return manager->template IsRegionModified<Type::CPU>(offset, size);
            });
    }

    /// Returns true if a region has been modified from the GPU
    [[nodiscard]] bool IsRegionGpuModified(VAddr query_cpu_addr, u64 query_size) noexcept {
        return IteratePages<false>(
            query_cpu_addr, query_size, [](Manager* manager, u64 offset, size_t size) {
                return manager->template IsRegionModified<Type::GPU>(offset, size);
            });
    }

    /// Returns true if a region has been marked as Preflushable
    [[nodiscard]] bool IsRegionPreflushable(VAddr query_cpu_addr, u64 query_size) noexcept {
        return IteratePages<false>(
            query_cpu_addr, query_size, [](Manager* manager, u64 offset, size_t size) {
                return manager->template IsRegionModified<Type::Preflushable>(offset, size);
            });
    }

    /// Mark region as CPU modified, notifying the device_tracker about this change
    void MarkRegionAsCpuModified(VAddr dirty_cpu_addr, u64 query_size) {
        IteratePages<true>(dirty_cpu_addr, query_size,
                           [](Manager* manager, u64 offset, size_t size) {
                               manager->template ChangeRegionState<Type::CPU, true>(
                                   manager->GetCpuAddr() + offset, size);
                           });
    }

    /// Unmark region as CPU modified, notifying the device_tracker about this change
    void UnmarkRegionAsCpuModified(VAddr dirty_cpu_addr, u64 query_size) {
        IteratePages<true>(dirty_cpu_addr, query_size,
                           [](Manager* manager, u64 offset, size_t size) {
                               manager->template ChangeRegionState<Type::CPU, false>(
                                   manager->GetCpuAddr() + offset, size);
                           });
    }

    /// Mark region as modified from the host GPU
    void MarkRegionAsGpuModified(VAddr dirty_cpu_addr, u64 query_size) noexcept {
        IteratePages<true>(dirty_cpu_addr, query_size,
                           [](Manager* manager, u64 offset, size_t size) {
                               manager->template ChangeRegionState<Type::GPU, true>(
                                   manager->GetCpuAddr() + offset, size);
                           });
    }

    /// Mark region as modified from the host GPU
    void MarkRegionAsPreflushable(VAddr dirty_cpu_addr, u64 query_size) noexcept {
        IteratePages<true>(dirty_cpu_addr, query_size,
                           [](Manager* manager, u64 offset, size_t size) {
                               manager->template ChangeRegionState<Type::Preflushable, true>(
                                   manager->GetCpuAddr() + offset, size);
                           });
    }

    /// Unmark region as modified from the host GPU
    void UnmarkRegionAsGpuModified(VAddr dirty_cpu_addr, u64 query_size) noexcept {
        IteratePages<true>(dirty_cpu_addr, query_size,
                           [](Manager* manager, u64 offset, size_t size) {
                               manager->template ChangeRegionState<Type::GPU, false>(
                                   manager->GetCpuAddr() + offset, size);
                           });
    }

    /// Unmark region as modified from the host GPU
    void UnmarkRegionAsPreflushable(VAddr dirty_cpu_addr, u64 query_size) noexcept {
        IteratePages<true>(dirty_cpu_addr, query_size,
                           [](Manager* manager, u64 offset, size_t size) {
                               manager->template ChangeRegionState<Type::Preflushable, false>(
                                   manager->GetCpuAddr() + offset, size);
                           });
    }

    /// Mark region as modified from the CPU
    /// but don't mark it as modified until FlusHCachedWrites is called.
    void CachedCpuWrite(VAddr dirty_cpu_addr, u64 query_size) {
        IteratePages<true>(
            dirty_cpu_addr, query_size, [this](Manager* manager, u64 offset, size_t size) {
                const VAddr cpu_address = manager->GetCpuAddr() + offset;
                manager->template ChangeRegionState<Type::CachedCPU, true>(cpu_address, size);
                cached_pages.insert(static_cast<u32>(cpu_address >> HIGHER_PAGE_BITS));
            });
    }

    /// Flushes cached CPU writes, and notify the device_tracker about the deltas
    void FlushCachedWrites(VAddr query_cpu_addr, u64 query_size) noexcept {
        IteratePages<false>(query_cpu_addr, query_size,
                            [](Manager* manager, [[maybe_unused]] u64 offset,
                               [[maybe_unused]] size_t size) { manager->FlushCachedWrites(); });
    }

    void FlushCachedWrites() noexcept {
        for (auto id : cached_pages) {
            top_tier[id]->FlushCachedWrites();
        }
        cached_pages.clear();
    }

    /// Call 'func' for each CPU modified range and unmark those pages as CPU modified
    template <typename Func>
    void ForEachUploadRange(VAddr query_cpu_range, u64 query_size, Func&& func) {
        IteratePages<true>(query_cpu_range, query_size,
                           [&func](Manager* manager, u64 offset, size_t size) {
                               manager->template ForEachModifiedRange<Type::CPU, true>(
                                   manager->GetCpuAddr() + offset, size, func);
                           });
    }

    /// Call 'func' for each GPU modified range and unmark those pages as GPU modified
    template <typename Func>
    void ForEachDownloadRange(VAddr query_cpu_range, u64 query_size, bool clear, Func&& func) {
        IteratePages<false>(query_cpu_range, query_size,
                            [&func, clear](Manager* manager, u64 offset, size_t size) {
                                if (clear) {
                                    manager->template ForEachModifiedRange<Type::GPU, true>(
                                        manager->GetCpuAddr() + offset, size, func);
                                } else {
                                    manager->template ForEachModifiedRange<Type::GPU, false>(
                                        manager->GetCpuAddr() + offset, size, func);
                                }
                            });
    }

    template <typename Func>
    void ForEachDownloadRangeAndClear(VAddr query_cpu_range, u64 query_size, Func&& func) {
        IteratePages<false>(query_cpu_range, query_size,
                            [&func](Manager* manager, u64 offset, size_t size) {
                                manager->template ForEachModifiedRange<Type::GPU, true>(
                                    manager->GetCpuAddr() + offset, size, func);
                            });
    }

private:
    template <bool create_region_on_fail, typename Func>
    bool IteratePages(VAddr cpu_address, size_t size, Func&& func) {
        using FuncReturn = typename std::invoke_result<Func, Manager*, u64, size_t>::type;
        static constexpr bool BOOL_BREAK = std::is_same_v<FuncReturn, bool>;
        std::size_t remaining_size{size};
        std::size_t page_index{cpu_address >> HIGHER_PAGE_BITS};
        u64 page_offset{cpu_address & HIGHER_PAGE_MASK};
        while (remaining_size > 0) {
            const std::size_t copy_amount{
                std::min<std::size_t>(HIGHER_PAGE_SIZE - page_offset, remaining_size)};
            auto* manager{top_tier[page_index]};
            if (manager) {
                if constexpr (BOOL_BREAK) {
                    if (func(manager, page_offset, copy_amount)) {
                        return true;
                    }
                } else {
                    func(manager, page_offset, copy_amount);
                }
            } else if constexpr (create_region_on_fail) {
                CreateRegion(page_index);
                manager = top_tier[page_index];
                if constexpr (BOOL_BREAK) {
                    if (func(manager, page_offset, copy_amount)) {
                        return true;
                    }
                } else {
                    func(manager, page_offset, copy_amount);
                }
            }
            page_index++;
            page_offset = 0;
            remaining_size -= copy_amount;
        }
        return false;
    }

    template <bool create_region_on_fail, typename Func>
    std::pair<u64, u64> IteratePairs(VAddr cpu_address, size_t size, Func&& func) {
        std::size_t remaining_size{size};
        std::size_t page_index{cpu_address >> HIGHER_PAGE_BITS};
        u64 page_offset{cpu_address & HIGHER_PAGE_MASK};
        u64 begin = std::numeric_limits<u64>::max();
        u64 end = 0;
        while (remaining_size > 0) {
            const std::size_t copy_amount{
                std::min<std::size_t>(HIGHER_PAGE_SIZE - page_offset, remaining_size)};
            auto* manager{top_tier[page_index]};
            const auto execute = [&] {
                auto [new_begin, new_end] = func(manager, page_offset, copy_amount);
                if (new_begin != 0 || new_end != 0) {
                    const u64 base_address = page_index << HIGHER_PAGE_BITS;
                    begin = std::min(new_begin + base_address, begin);
                    end = std::max(new_end + base_address, end);
                }
            };
            if (manager) {
                execute();
            } else if constexpr (create_region_on_fail) {
                CreateRegion(page_index);
                manager = top_tier[page_index];
                execute();
            }
            page_index++;
            page_offset = 0;
            remaining_size -= copy_amount;
        }
        if (begin < end) {
            return std::make_pair(begin, end);
        } else {
            return std::make_pair(0ULL, 0ULL);
        }
    }

    void CreateRegion(std::size_t page_index) {
        const VAddr base_cpu_addr = page_index << HIGHER_PAGE_BITS;
        top_tier[page_index] = GetNewManager(base_cpu_addr);
    }

    Manager* GetNewManager(VAddr base_cpu_address) {
        const auto on_return = [&] {
            auto* new_manager = free_managers.front();
            new_manager->SetCpuAddress(base_cpu_address);
            free_managers.pop_front();
            return new_manager;
        };
        if (!free_managers.empty()) {
            return on_return();
        }
        manager_pool.emplace_back();
        auto& last_pool = manager_pool.back();
        for (size_t i = 0; i < MANAGER_POOL_SIZE; i++) {
            new (&last_pool[i]) Manager(0, *device_tracker, HIGHER_PAGE_SIZE);
            free_managers.push_back(&last_pool[i]);
        }
        return on_return();
    }

    std::deque<std::array<Manager, MANAGER_POOL_SIZE>> manager_pool;
    std::deque<Manager*> free_managers;

    std::array<Manager*, NUM_HIGH_PAGES> top_tier{};

    std::unordered_set<u32> cached_pages;

    DeviceTracker* device_tracker = nullptr;
};

} // namespace VideoCommon
