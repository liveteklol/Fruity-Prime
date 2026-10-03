#include "../NativeRuntime/Rhi/Vulkan/VulkanDescriptorAllocator.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using Allocator = Vulkan::VulkanDescriptorAllocator;
    void Expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    template<class T> T Handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
    const auto Layout = Handle<VkDescriptorSetLayout>(3);
    struct NativeFailure : std::runtime_error
    {
        VkResult Code;
        NativeFailure(VkResult code, const char* message) : std::runtime_error(message), Code(code) {}
    };
    void Check(VkResult result, const char* operation)
    { if (result != VK_SUCCESS) throw NativeFailure(result, operation); }
    struct Driver final
    {
        struct Page final { std::uint32_t MaxSets = 0; std::map<VkDescriptorType, unsigned> Counts; };
        std::map<VkDescriptorPool, Page> Pages;
        std::vector<VkDescriptorPool> Allocations;
        std::vector<VkResult> Results;
        VkResult CreateResult = VK_SUCCESS, ResetResult = VK_SUCCESS;
        unsigned Creates = 0, Resets = 0, Destroys = 0, ResultIndex = 0;
        std::uintptr_t Next = 10;
    };
    Driver* Active = nullptr;
    VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice, const VkDescriptorPoolCreateInfo* info,
        const VkAllocationCallbacks*, VkDescriptorPool* output)
    {
        ++Active->Creates;
        if (Active->CreateResult != VK_SUCCESS) return Active->CreateResult;
        Driver::Page page; page.MaxSets = info->maxSets;
        Expect(info->flags == 0, "Pool does not use bulk reset policy.");
        for (unsigned i = 0; i < info->poolSizeCount; ++i)
        {
            Expect(info->pPoolSizes[i].descriptorCount > 0, "Zero native descriptor count.");
            Expect(page.Counts.emplace(info->pPoolSizes[i].type, info->pPoolSizes[i].descriptorCount).second,
                "Duplicate native descriptor type.");
        }
        *output = Handle<VkDescriptorPool>(++Active->Next);
        Active->Pages.emplace(*output, page); return VK_SUCCESS;
    }
    VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice, VkDescriptorPool pool, const VkAllocationCallbacks*)
    { Expect(Active->Pages.erase(pool) == 1, "Pool destroyed twice or after owner lifetime."); ++Active->Destroys; }
    VKAPI_ATTR VkResult VKAPI_CALL Reset(VkDevice, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags)
    {
        Expect(Active->Pages.contains(pool) && flags == 0, "Invalid reset.");
        ++Active->Resets; return Active->ResetResult;
    }
    VKAPI_ATTR VkResult VKAPI_CALL Allocate(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* output)
    {
        Expect(Active->Pages.contains(info->descriptorPool), "Allocation on a closed pool.");
        Expect(info->descriptorSetCount > 0, "Empty descriptor batch.");
        for (unsigned i = 0; i < info->descriptorSetCount; ++i)
            Expect(info->pSetLayouts[i] == Layout, "Wrong descriptor layout.");
        Active->Allocations.push_back(info->descriptorPool);
        const auto result = Active->ResultIndex < Active->Results.size()
            ? Active->Results[Active->ResultIndex++] : VK_SUCCESS;
        for (unsigned i = 0; i < info->descriptorSetCount; ++i)
            output[i] = result == VK_SUCCESS ? Handle<VkDescriptorSet>(++Active->Next) : VK_NULL_HANDLE;
        return result;
    }
    Allocator::Dispatch Dispatch()
    { return {Handle<VkDevice>(1), Create, Destroy, Reset, Allocate, Check}; }
    BindingLayoutDesc Need(BindingType type, unsigned count = 1)
    { return {{{0, type, ShaderStage::Fragment, count}}}; }
    template<class Error, class F> void Reject(F action)
    {
        bool rejected = false;
        try { action(); } catch (const Error&) { rejected = true; }
        Expect(rejected, "Invalid operation was accepted.");
    }
    template<class F> void NativeReject(VkResult code, F action)
    {
        bool rejected = false;
        try { action(); } catch (const NativeFailure& error) { rejected = error.Code == code; }
        Expect(rejected, "Native error was lost or masked as descriptor exhaustion.");
    }

    void CapacityAndReuse()
    {
        Driver driver; Active = &driver;
        Allocator allocator(Dispatch(), {2, {2, 2, 2, 2, 2}});
        Reject<std::logic_error>([&] { allocator.Allocate(Layout, {}); });
        allocator.ResetAfterCompletion({0});
        const auto first = allocator.Allocate(Layout, Need(BindingType::SampledTexture, 2));
        const auto second = allocator.Allocate(Layout, Need(BindingType::SampledTexture));
        Expect(first != second && allocator.PageCount() == 2 && driver.Allocations[0] != driver.Allocations[1],
            "Per-type exhaustion did not use another page.");
        allocator.Allocate(Layout, {}); // last set of page 2
        allocator.Allocate(Layout, {}); // maxSets, despite spare descriptor capacity
        Expect(allocator.PageCount() == 3, "Set-count exhaustion was ignored.");
        allocator.Submitted({9});
        Reject<std::logic_error>([&] { allocator.Allocate(Layout, {}); });
        Reject<std::logic_error>([&] { allocator.ResetAfterCompletion({8}); });
        Expect(driver.Resets == 0 && allocator.LastUse() == SubmissionSerial{9}, "In-flight reset reached native API.");
        allocator.ResetAfterCompletion({9});
        Expect(driver.Resets == 3, "Overflow pages were not recycled.");
        allocator.Allocate(Layout, Need(BindingType::SampledTexture, 2));
        Expect(driver.Creates == 3 && driver.Allocations.back() == driver.Allocations.front(), "Completed page not reused.");
        Reject<std::logic_error>([&] { allocator.Submitted({9}); });
        allocator.Submitted({10});
        allocator.ResetAfterCompletion({11});
        allocator.Close(); allocator.Close();
        Expect(driver.Pages.empty() && driver.Destroys == 3 && allocator.PageCount() == 0, "Native close leaked pages.");
        Reject<std::logic_error>([&] { allocator.ResetAfterCompletion({12}); });
        Reject<std::logic_error>([&] { allocator.Allocate(Layout, {}); });
    }

    void AdmissionAndErrors()
    {
        Driver driver; Active = &driver;
        Allocator allocator(Dispatch(), {2, {1, 0, 1, 0, 1}});
        allocator.ResetAfterCompletion({0});
        BindingLayoutDesc wide;
        for (unsigned type = 0; type < 5; ++type)
            wide.entries.push_back({type, static_cast<BindingType>(type), ShaderStage::Fragment, 7});
        allocator.Allocate(Layout, wide);
        const auto& page = driver.Pages.begin()->second;
        Expect(page.Counts.size() == 5, "Descriptor types not admitted.");
        for (const auto& [type, count] : page.Counts) Expect(count == 7, "Page did not admit whole large layout.");
        auto overflow = Need(BindingType::Sampler, UINT32_MAX);
        overflow.entries.push_back({1, BindingType::Sampler, ShaderStage::Fragment, 1});
        const auto created = driver.Creates;
        Reject<std::invalid_argument>([&] { allocator.Allocate(Layout, overflow); });
        Reject<std::invalid_argument>([&] { allocator.Allocate(Layout, Need(BindingType::Sampler, 0)); });
        Reject<std::invalid_argument>([&] { allocator.Allocate(Layout, Need(static_cast<BindingType>(99))); });
        Expect(driver.Creates == created, "Invalid requirements reached native admission.");
        driver.CreateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        NativeReject(VK_ERROR_OUT_OF_DEVICE_MEMORY, [&] { allocator.Allocate(Layout, wide); });
        Expect(allocator.PageCount() == 1, "Failed page admission changed ownership.");
        driver.CreateResult = VK_SUCCESS;
        allocator.Allocate(Layout, wide);
        Expect(allocator.PageCount() == 2, "Allocator could not recover after failed admission.");
        allocator.ResetAfterCompletion({0});
        driver.Results = {VK_ERROR_OUT_OF_HOST_MEMORY}; driver.ResultIndex = 0;
        NativeReject(VK_ERROR_OUT_OF_HOST_MEMORY, [&] { allocator.Allocate(Layout, {}); });
        Expect(allocator.PageCount() == 2, "Host OOM caused overflow growth.");
        driver.Results.clear();
        driver.ResetResult = VK_ERROR_DEVICE_LOST;
        NativeReject(VK_ERROR_DEVICE_LOST, [&] { allocator.ResetAfterCompletion({0}); });
        Reject<std::logic_error>([&] { allocator.Allocate(Layout, {}); });
        driver.ResetResult = VK_SUCCESS;
        allocator.ResetAfterCompletion({0});
        allocator.Allocate(Layout, {});
    }

    void FragmentationAndBoundedGrowth()
    {
        Driver driver; Active = &driver;
        Allocator allocator(Dispatch(), {4, {4, 4, 4, 4, 4}});
        allocator.ResetAfterCompletion({0}); allocator.Allocate(Layout, {});
        driver.Results = {VK_ERROR_FRAGMENTED_POOL, VK_SUCCESS};
        allocator.Allocate(Layout, {});
        Expect(allocator.PageCount() == 2, "Fragmented page was not replaced.");
        allocator.ResetAfterCompletion({0});
        driver.Results = {VK_ERROR_OUT_OF_POOL_MEMORY, VK_ERROR_FRAGMENTED_POOL, VK_ERROR_OUT_OF_POOL_MEMORY};
        driver.ResultIndex = 0;
        NativeReject(VK_ERROR_OUT_OF_POOL_MEMORY, [&] { allocator.Allocate(Layout, {}); });
        Expect(driver.ResultIndex == 3 && allocator.PageCount() == 3, "Fresh failure caused unbounded page growth.");
        driver.Results.clear(); allocator.ResetAfterCompletion({0});
        allocator.Allocate(Layout, {});
    }

    void FixedSlots()
    {
        Driver driver; Active = &driver;
        Allocator allocator(Dispatch(), {2, {2, 0, 2, 0, 2}});
        const BindingLayoutDesc abi{{{0, BindingType::UniformBuffer, ShaderStage::AllGraphics, 3},
            {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
            {2, BindingType::Sampler, ShaderStage::Fragment, 1}}};
        Expect(allocator.Preallocate(77, Layout, abi, 4), "Fixed admission failed.");
        const auto counts = driver.Pages.begin()->second.Counts;
        Expect(counts.at(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) == 12
            && counts.at(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) == 4 && counts.at(VK_DESCRIPTOR_TYPE_SAMPLER) == 4,
            "Fixed pool counts differ from actual ABI binding counts.");
        Reject<std::logic_error>([&] { allocator.AllocateFixed(77); });
        allocator.ResetAfterCompletion({0});
        const auto first = allocator.AllocateFixed(77);
        for (unsigned i = 1; i < 4; ++i) Expect(allocator.AllocateFixed(77) != first, "Fixed set overwritten before completion.");
        Expect(!allocator.AllocateFixed(77) && !allocator.AllocateFixed(99), "Fixed overflow was not reported.");
        Expect(driver.Allocations.size() == 1 && driver.Resets == 0, "Fixed hot path allocated/reset native sets.");
        allocator.Submitted({8});
        allocator.RetireFixed(77);
        Reject<std::logic_error>([&] { allocator.ResetAfterCompletion({7}); });
        Expect(driver.Destroys == 0, "In-flight fixed pool was destroyed.");
        allocator.ResetAfterCompletion({8});
        Expect(driver.Destroys == 1 && !allocator.AllocateFixed(77), "Completed retirement leaked fixed pool.");
        Expect(allocator.Preallocate(78, Layout, abi, 4), "New fixed generation could not be admitted.");
        const auto reused = allocator.AllocateFixed(78);
        allocator.Submitted({9}); allocator.ResetAfterCompletion({9});
        Expect(allocator.AllocateFixed(78) == reused && driver.Resets == 0, "Completed fixed set was reallocated.");
        Reject<std::invalid_argument>([&] { allocator.Preallocate(90, Layout, abi, 0); });
        Reject<std::invalid_argument>([&] { allocator.Preallocate(90, Layout, Need(BindingType::Sampler, UINT32_MAX), 4); });
        allocator.Close(); Expect(driver.Pages.empty(), "Fixed close leaked a pool.");
        Allocator fallback(Dispatch(), {2, {2, 0, 2, 0, 2}}); fallback.ResetAfterCompletion({0});
        driver.CreateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        Expect(!fallback.Preallocate(100, Layout, abi, 32), "Optional pool OOM killed fallback.");
        const auto creates = driver.Creates;
        driver.CreateResult = VK_SUCCESS;
        Expect(!fallback.Preallocate(100, Layout, abi, 32) && driver.Creates == creates, "Failed fixed admission retried every draw.");
        Expect(!fallback.AllocateFixed(100) && fallback.Allocate(Layout, abi), "Ordinary allocator unavailable after optional OOM.");
        driver.Results.push_back(VK_ERROR_OUT_OF_POOL_MEMORY);
        Expect(!fallback.Preallocate(101, Layout, abi, 32), "Optional fixed set capacity error killed fallback.");
        Expect(!fallback.AllocateFixed(101) && fallback.Allocate(Layout, abi), "Fixed allocation failure leaked into draw path.");
        driver.CreateResult = VK_ERROR_DEVICE_LOST;
        NativeReject(VK_ERROR_DEVICE_LOST, [&] { fallback.Preallocate(102, Layout, abi, 32); });
        driver.CreateResult = VK_SUCCESS; driver.Results.push_back(VK_ERROR_UNKNOWN);
        NativeReject(VK_ERROR_UNKNOWN, [&] { fallback.Preallocate(103, Layout, abi, 32); });
        fallback.Close(); Expect(driver.Pages.empty(), "Optional failure leaked native pool.");
    }

    void IndependentSlots()
    {
        Driver driver; Active = &driver;
        Allocator a(Dispatch(), {1, {1, 0, 1, 0, 1}}), b(Dispatch(), {1, {1, 0, 1, 0, 1}});
        a.ResetAfterCompletion({0}); b.ResetAfterCompletion({0});
        a.Allocate(Layout, {}); const auto aPage = driver.Allocations.back(); a.Submitted({3});
        b.Allocate(Layout, {}); const auto bPage = driver.Allocations.back(); b.Submitted({4});
        a.ResetAfterCompletion({3}); a.Allocate(Layout, {});
        Expect(aPage != bPage && driver.Allocations.back() == aPage, "Slots shared pools or recycle state.");
        Reject<std::logic_error>([&] { b.ResetAfterCompletion({3}); });
        b.ResetAfterCompletion({4});
        a.Close(); b.Close(); Expect(driver.Pages.empty(), "Independent slot close leaked native pages.");
    }
}
int main()
{
    try
    {
        CapacityAndReuse(); AdmissionAndErrors(); FragmentationAndBoundedGrowth(); FixedSlots(); IndependentSlots();
        std::cout << "Vulkan descriptor allocator: capacity/reuse/admission/fault/slot contracts PASS\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
