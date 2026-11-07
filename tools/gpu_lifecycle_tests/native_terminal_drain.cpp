// SPDX-License-Identifier: GPL-2.0-or-later
// Native primitive witness, not an emulator/Guest admission test.
#define WIN32_LEAN_AND_MEAN
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

static void Check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(result));
}

int main(int argc, char** argv) {
    try {
        const bool omit_tail=argc==2 && std::string_view{argv[1]}=="--omit-tail";
        if(argc>1 && !omit_tail) throw std::runtime_error("Unknown probe argument");
        const auto library = LoadLibraryW(L"vulkan-1.dll");
        if (!library) { std::cerr << "UNAVAILABLE Vulkan loader\n"; return 77; }
#define LOAD(name) const auto name = std::bit_cast<PFN_##name>(GetProcAddress(library, #name)); \
    if (!name) throw std::runtime_error("Missing " #name)
        LOAD(vkCreateInstance); LOAD(vkEnumeratePhysicalDevices); LOAD(vkGetPhysicalDeviceProperties);
        LOAD(vkGetPhysicalDeviceQueueFamilyProperties); LOAD(vkGetPhysicalDeviceMemoryProperties);
        LOAD(vkCreateDevice); LOAD(vkGetDeviceQueue); LOAD(vkCreateBuffer);
        LOAD(vkGetBufferMemoryRequirements); LOAD(vkAllocateMemory); LOAD(vkBindBufferMemory);
        LOAD(vkMapMemory); LOAD(vkCreateCommandPool); LOAD(vkAllocateCommandBuffers);
        LOAD(vkBeginCommandBuffer); LOAD(vkCmdFillBuffer); LOAD(vkCmdPipelineBarrier);
        LOAD(vkCmdCopyBuffer); LOAD(vkEndCommandBuffer); LOAD(vkQueueSubmit);
        LOAD(vkDeviceWaitIdle); LOAD(vkInvalidateMappedMemoryRanges); LOAD(vkUnmapMemory);
        LOAD(vkDestroyCommandPool); LOAD(vkDestroyBuffer); LOAD(vkFreeMemory);
        LOAD(vkDestroyDevice); LOAD(vkDestroyInstance);
#undef LOAD
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        VkInstance instance{}; Check(vkCreateInstance(&instance_info, nullptr, &instance));
        uint32_t count{}; Check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        if (!count) { std::cerr << "UNAVAILABLE Vulkan device\n"; return 77; }
        std::vector<VkPhysicalDevice> devices(count);
        Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        const auto physical = devices.front();
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical, &properties);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        uint32_t family{};
        while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
        if (family == count) { std::cerr << "UNAVAILABLE graphics queue\n"; return 77; }
        const float priority=1;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex=family; queue_info.queueCount=1; queue_info.pQueuePriorities=&priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount=1; device_info.pQueueCreateInfos=&queue_info;
        VkDevice device{}; Check(vkCreateDevice(physical,&device_info,nullptr,&device));
        VkQueue queue{}; vkGetDeviceQueue(device,family,0,&queue);
        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical,&memory_properties);
        std::array<VkBuffer,2> buffers{}; std::array<VkDeviceMemory,2> allocations{};
        void* mapped{}; bool coherent{};
        for (size_t i=0;i<buffers.size();++i) {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size=4096; info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            Check(vkCreateBuffer(device,&info,nullptr,&buffers[i]));
            VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device,buffers[i],&requirements);
            uint32_t type=UINT32_MAX;
            // Prefer noncoherent memory to exercise explicit visibility maintenance where available.
            for (uint32_t t=0;t<memory_properties.memoryTypeCount;++t) {
                const auto flags=memory_properties.memoryTypes[t].propertyFlags;
                if ((requirements.memoryTypeBits&(1u<<t)) && (flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                    type=t; if (!(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) break;
                }
            }
            if(type==UINT32_MAX) throw std::runtime_error("No host-visible buffer memory");
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=type;
            Check(vkAllocateMemory(device,&allocation,nullptr,&allocations[i]));
            Check(vkBindBufferMemory(device,buffers[i],allocations[i],0));
            if(i==1) {
                coherent=(memory_properties.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
                Check(vkMapMemory(device,allocations[i],0,VK_WHOLE_SIZE,0,&mapped));
            }
        }
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.queueFamilyIndex=family;
        VkCommandPool pool{}; Check(vkCreateCommandPool(device,&pool_info,nullptr,&pool));
        VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command_info.commandPool=pool; command_info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_info.commandBufferCount=2;
        std::array<VkCommandBuffer,2> commands{};
        Check(vkAllocateCommandBuffers(device,&command_info,commands.data()));
        for(size_t i=0;i<commands.size();++i) {
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            Check(vkBeginCommandBuffer(commands[i],&begin));
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(commands[i],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
            vkCmdFillBuffer(commands[i],buffers[0],0,4096,i==0?0x37373737u:0x99999999u);
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(commands[i],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
            const VkBufferCopy copy{0,0,4096}; vkCmdCopyBuffer(commands[i],buffers[0],buffers[1],1,&copy);
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(commands[i],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
            Check(vkEndCommandBuffer(commands[i]));
        }
        VkResult submission=VK_NOT_READY;
        std::thread publisher([&] {
            for(size_t index=0;index<(omit_tail?1u:commands.size());++index) {
                const auto command=commands[index];
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.commandBufferCount=1; submit.pCommandBuffers=&command;
                submission=vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE);
                if(submission!=VK_SUCCESS) break;
            }
        });
        publisher.join(); Check(submission); // No remaining publisher, all native owners still alive.
        Check(vkDeviceWaitIdle(device));
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory=allocations[1]; range.size=VK_WHOLE_SIZE;
        Check(vkInvalidateMappedMemoryRanges(device,1,&range));
        const auto* words=static_cast<const uint32_t*>(mapped);
        for(size_t i=0;i<1024;++i) if(words[i]!=0x99999999u) throw std::runtime_error("Tail copy content mismatch");
        vkUnmapMemory(device,allocations[1]); vkDestroyCommandPool(device,pool,nullptr);
        for(size_t i=0;i<buffers.size();++i) {vkDestroyBuffer(device,buffers[i],nullptr); vkFreeMemory(device,allocations[i],nullptr);}
        vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr); FreeLibrary(library);
        std::cout<<"PASS native terminal drain device="<<properties.deviceName<<" coherent="<<coherent
                 <<" submissions=2 checked_words=1024 owners_released_after_visibility=1\n";
        return 0;
    } catch(const std::exception& error) {
        // No unproven native cleanup on failure. The isolated process owns all allocations.
        std::cerr<<"FAIL "<<error.what()<<"\n"; return 1;
    }
}
