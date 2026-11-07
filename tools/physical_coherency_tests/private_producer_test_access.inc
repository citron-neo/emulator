// Privileged inspection ONLY for isolated tests; never included by production.
namespace Vulkan {
class PrivateProducerTestAccess {
public:
    static auto KernelState() {
        auto state=PrivateProducerGuard::Create(64);
        if(state) {state->count=2;state->identities[0]=reinterpret_cast<void*>(1);state->identities[1]=reinterpret_cast<void*>(2);}
        return state;
    }
    static auto Acquire(const auto& state){return PrivateProducerGuard::Acquire(state);}
    static void* Page(const auto& state){return state->page;}
    static bool Complete(const auto& guard){return guard->CompleteChecked();} // Injected unit certificate only.
    static void* Page(const Scheduler::PrivateProducerDomain& domain){return domain.backing->page;}
    static auto Operation(const Scheduler::PrivateProducerDomain& domain){return domain.operation.lock();}
    static auto Targets(const Scheduler::PrivateProducerDomain& domain){auto op=domain.operation.lock();return op?op->resources:std::vector<Scheduler::TransferBuffer>{};}
    static auto WeakState(const Scheduler::PrivateProducerDomain& domain){return std::weak_ptr{domain.backing};}
    static bool Current(Scheduler::PrivateProducerDomain& domain,const Scheduler::Transfer& transfer,u32 slot,u64 offset,u64 size){
        return domain.scheduler->CurrentTransferContentRange(transfer,*domain.backing,slot,offset,size)!=nullptr;
    }
    static bool GuestMappingMatches(Scheduler::PrivateProducerDomain& domain,const auto& memory){
        const auto op=domain.operation.lock();return op && !op->resources.empty() && op->resources[0].physical->MatchesCurrent(memory);
    }
    static void InvalidateEpoch(Scheduler::PrivateProducerDomain& domain){domain.backing->epoch->generation.fetch_add(2);}
    static void DropGuard(const Scheduler::Transfer& transfer){transfer->exclusion.reset();}
};
} // namespace Vulkan
