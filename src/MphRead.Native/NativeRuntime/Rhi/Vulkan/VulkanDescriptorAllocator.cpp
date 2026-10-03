#include "VulkanDescriptorAllocator.hpp"

#if defined(FRUITY_HAS_VULKAN)
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <chrono>
#include <iostream>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    VulkanDescriptorAllocator::VulkanDescriptorAllocator(Dispatch dispatch, Capacity capacity)
        : _dispatch(dispatch), _capacity(capacity)
    {
        if (!dispatch.Device || !dispatch.Create || !dispatch.Destroy || !dispatch.Reset
            || !dispatch.Allocate || !dispatch.CheckResult || capacity.Sets == 0)
            throw std::invalid_argument("Vulkan descriptor allocator: invalid dispatch or capacity.");
    }

    void VulkanDescriptorAllocator::RequireOpen() const
    { if (_closed) throw std::logic_error("Vulkan descriptor allocator has ended."); }

    VulkanDescriptorAllocator::Counts VulkanDescriptorAllocator::Requirements(const BindingLayoutDesc& desc)
    {
        Counts needed{};
        for (const auto& entry : desc.entries)
        {
            const auto index = static_cast<std::size_t>(entry.type);
            if (index >= needed.size() || entry.count == 0)
                throw std::invalid_argument("Vulkan descriptor allocator: invalid binding declaration.");
            needed[index] += entry.count;
            if (needed[index] > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Vulkan descriptor allocator: descriptor count overflow.");
        }
        return needed;
    }

    void VulkanDescriptorAllocator::AddPage(const Counts& needed)
    {
        constexpr std::array<VkDescriptorType, 5> types{
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER};
        std::array<VkDescriptorPoolSize, 5> sizes{};
        std::uint32_t sizeCount = 0;
        Page page{};
        for (std::size_t i = 0; i < needed.size(); ++i)
        {
            page.Capacity[i] = std::max(_capacity.Counts[i], static_cast<std::uint32_t>(needed[i]));
            if (page.Capacity[i]) sizes[sizeCount++] = {types[i], page.Capacity[i]};
        }
        VkDescriptorPoolCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        create.maxSets = _capacity.Sets;
        create.poolSizeCount = sizeCount;
        create.pPoolSizes = sizeCount ? sizes.data() : nullptr;
        // Reserve before native admission: retaining a successfully created
        // pool must not allocate or throw, including during overflow growth.
        _pages.reserve(_pages.size() + 1);
        _dispatch.CheckResult(_dispatch.Create(_dispatch.Device, &create, nullptr, &page.Pool),
            "vkCreateDescriptorPool(allocator)");
        _pages.push_back(page);
    }

    VkDescriptorSet VulkanDescriptorAllocator::Allocate(VkDescriptorSetLayout layout, const BindingLayoutDesc& desc)
    {
        RequireOpen();
        if (!_ready || !layout) throw std::logic_error("Vulkan descriptor allocator: slot is not recording.");
        const auto needed = Requirements(desc);
        for (;;)
        {
            bool fresh = false;
            if (_active == _pages.size()) { AddPage(needed); fresh = true; }
            auto& page = _pages[_active];
            bool available = page.Sets < _capacity.Sets;
            for (std::size_t i = 0; i < needed.size(); ++i)
                available = available && needed[i] <= page.Capacity[i] - page.Used[i];
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = page.Pool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &layout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            const auto result = available ? _dispatch.Allocate(_dispatch.Device, &allocate, &set)
                : VK_ERROR_OUT_OF_POOL_MEMORY;
            if (result == VK_SUCCESS)
            {
                ++page.Sets;
                for (std::size_t i = 0; i < needed.size(); ++i) page.Used[i] += needed[i];
                return set;
            }
            // System/device OOM is not descriptor exhaustion. A fresh page
            // already admits this whole layout: repeated failure must not grow
            // unboundedly or mask the native error.
            if (fresh || (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL))
                _dispatch.CheckResult(result, "vkAllocateDescriptorSets(allocator)");
            ++_active;
        }
    }

    void VulkanDescriptorAllocator::Submitted(SubmissionSerial serial)
    {
        RequireOpen();
        if (!_ready || !serial.Value || serial <= _lastUse)
            throw std::logic_error("Vulkan descriptor allocator: invalid submitted generation.");
        _lastUse = serial;
        _ready = false;
    }

    bool VulkanDescriptorAllocator::Preallocate(std::uint64_t identity, VkDescriptorSetLayout layout,
        const BindingLayoutDesc& desc, std::uint32_t count)
    {
        RequireOpen();
        if (!identity || !layout || !count || count > 1024)
            throw std::invalid_argument("Invalid fixed descriptor admission.");
        if (const auto found = _fixed.find(identity); found != _fixed.end()) return !found->second.Sets.empty();
        // At most 64 fixed layouts per stream slot. Other programs use the
        // ordinary allocator; admission is never allowed to grow this cache.
        if (_fixed.size() == 64) return false;
        const auto requirements = Requirements(desc);
        constexpr std::array<VkDescriptorType, 5> types{
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER};
        std::vector<VkDescriptorPoolSize> sizes;
        for (std::size_t i = 0; i < requirements.size(); ++i)
        {
            if (requirements[i] > UINT32_MAX / count)
                throw std::invalid_argument("Fixed descriptor count overflow.");
            if (requirements[i]) sizes.push_back({types[i], static_cast<std::uint32_t>(requirements[i] * count)});
        }
        const auto started = _metrics ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const auto elapsed = [&] { if (_metrics) _fixedSetupNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count(); };
        const auto capacityFailure = [](VkResult value) {
            return value == VK_ERROR_OUT_OF_HOST_MEMORY || value == VK_ERROR_OUT_OF_DEVICE_MEMORY
                || value == VK_ERROR_OUT_OF_POOL_MEMORY || value == VK_ERROR_FRAGMENTED_POOL || value == VK_ERROR_FRAGMENTATION;
        };
        const auto unavailable = [&] {
            _fixed.emplace(identity, FixedPage{}); // Do not retry each draw.
            if (_metrics) ++_fixedFailures;
            elapsed(); return false;
        };
        FixedPage page;
        page.Sets.resize(count);
        const std::vector<VkDescriptorSetLayout> layouts(count, layout);
        VkDescriptorPoolCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        create.maxSets = count; create.poolSizeCount = static_cast<std::uint32_t>(sizes.size()); create.pPoolSizes = sizes.data();
        const auto created = _dispatch.Create(_dispatch.Device, &create, nullptr, &page.Pool);
        if (capacityFailure(created)) return unavailable();
        _dispatch.CheckResult(created, "vkCreateDescriptorPool(fixed ABI)");
        if (!page.Pool) throw std::runtime_error("Fixed descriptor pool creation returned no pool.");
        if (_metrics) ++_fixedPools;
        try
        {
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = page.Pool; allocate.descriptorSetCount = count; allocate.pSetLayouts = layouts.data();
            const auto allocated = _dispatch.Allocate(_dispatch.Device, &allocate, page.Sets.data());
            if (capacityFailure(allocated))
            {
                _dispatch.Destroy(_dispatch.Device, page.Pool, nullptr);
                page.Pool = VK_NULL_HANDLE;
                return unavailable();
            }
            _dispatch.CheckResult(allocated, "vkAllocateDescriptorSets(fixed ABI)");
            _fixed.emplace(identity, std::move(page));
        }
        catch (...) { if (page.Pool) _dispatch.Destroy(_dispatch.Device, page.Pool, nullptr); throw; }
        if (_metrics) _fixedReserved += count;
        elapsed(); return true;
    }

    VkDescriptorSet VulkanDescriptorAllocator::AllocateFixed(std::uint64_t identity)
    {
        RequireOpen();
        if (!_ready) throw std::logic_error("Fixed descriptor slot is not recording.");
        const auto found = _fixed.find(identity);
        if (found == _fixed.end() || found->second.Retired || found->second.Cursor == found->second.Sets.size())
        { if (_metrics) ++_fixedOverflow; return VK_NULL_HANDLE; }
        auto& page = found->second;
        const auto result = page.Sets[page.Cursor++];
        if (page.Cursor > page.HighWater)
        { if (_metrics) _fixedHighWater += page.Cursor - page.HighWater; page.HighWater = page.Cursor; }
        return result;
    }
    void VulkanDescriptorAllocator::RetireFixed(std::uint64_t identity)
    {
        RequireOpen();
        if (const auto found = _fixed.find(identity); found != _fixed.end()) found->second.Retired = true;
    }

    void VulkanDescriptorAllocator::ResetAfterCompletion(SubmissionSerial completed)
    {
        RequireOpen();
        if (completed < _lastUse)
            throw std::logic_error("Vulkan descriptor allocator: generation is still in flight.");
        _ready = false;
        for (auto it = _fixed.begin(); it != _fixed.end();)
        {
            if (it->second.Retired)
            {
                if (it->second.Pool) _dispatch.Destroy(_dispatch.Device, it->second.Pool, nullptr);
                it = _fixed.erase(it);
            }
            else { it->second.Cursor = 0; ++it; }
        }
        for (auto& page : _pages)
        {
            _dispatch.CheckResult(_dispatch.Reset(_dispatch.Device, page.Pool, 0),
                "vkResetDescriptorPool(allocator)");
            page.Sets = 0;
            page.Used.fill(0);
        }
        _active = 0;
        _ready = true;
    }

    void VulkanDescriptorAllocator::Close() noexcept
    {
        if (_closed) return;
        if (_metrics && (_fixedPools || _fixedFailures || _fixedOverflow))
            std::cout << "[fixed-descriptor-metrics] pools_created=" << _fixedPools << " sets_reserved=" << _fixedReserved
                << " sets_high_water=" << _fixedHighWater << " sets_unused=" << (_fixedReserved - _fixedHighWater)
                << " preallocation_failures=" << _fixedFailures << " overflow_to_generic=" << _fixedOverflow
                << " setup_time_ns=" << _fixedSetupNs << '\n';
        for (const auto& [identity, page] : _fixed) if (page.Pool) _dispatch.Destroy(_dispatch.Device, page.Pool, nullptr);
        _fixed.clear();
        for (const auto& page : _pages) _dispatch.Destroy(_dispatch.Device, page.Pool, nullptr);
        _pages.clear();
        _ready = false;
        _closed = true;
    }
}
#endif
