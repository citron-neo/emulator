// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Vulkan {
enum class NativeSourceStatus { Pending, Complete, Unknown };

// GPU-owner thread only. Receipts are supplied/interpreted only by Scheduler.
// A draw scope survives preparatory/internal Flush/Finish calls: every later
// recorded command renews its source owners in that command buffer's batch.
template <class Receipt>
class NativeSourceRetainer {
    struct Batch { Receipt receipt; std::vector<std::shared_ptr<const void>> owners; };
    std::function<Receipt()> make_receipt;
    std::vector<std::shared_ptr<const void>> active;
    std::vector<std::shared_ptr<Batch>> batches;
    std::shared_ptr<Batch> pending;
public:
    explicit NativeSourceRetainer(std::function<Receipt()> make) : make_receipt{std::move(make)} {}
    class Scope {
        NativeSourceRetainer* parent;
        size_t offset;
    public:
        explicit Scope(NativeSourceRetainer& owner) : parent{&owner},offset{owner.active.size()} {}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
        Scope(Scope&& other) noexcept :parent{std::exchange(other.parent,nullptr)},offset{other.offset}{}
        ~Scope(){if(parent)parent->active.resize(offset);}
    };
    Scope Begin(){return Scope{*this};}
    void Retain(const std::shared_ptr<const void>& owner){
        active.push_back(owner);
        BeforeRecord();
    }
    void BeforeRecord(){
        if(active.empty())return;
        if(!pending){
            auto next=std::make_shared<Batch>();next->receipt=make_receipt();
            batches.push_back(next);pending=std::move(next);
        }
        for(const auto& owner:active)
            if(std::find(pending->owners.begin(),pending->owners.end(),owner)==pending->owners.end())
                pending->owners.push_back(owner);
    }
    Receipt TakeSubmission(){
        if(!pending)return {};
        auto receipt=pending->receipt;pending.reset();return receipt;
    }
    template<class Poll>void Collect(Poll&& poll){
        std::erase_if(batches,[&](const auto& batch){
            return batch!=pending && poll(batch->receipt)==NativeSourceStatus::Complete;
        });
    }
    bool Empty()const{return batches.empty()&&active.empty();}
    // Called only after Scheduler admission/worker closure and checked device idle.
    bool RetireAfterIdle(){
        if(!active.empty())return false;
        pending.reset();batches.clear();return true;
    }
};
} // namespace Vulkan
