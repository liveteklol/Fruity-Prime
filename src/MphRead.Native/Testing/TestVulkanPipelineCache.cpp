#include "../NativeRuntime/Rhi/Vulkan/VulkanPipelineCache.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    using Cache = MphRead::NativeRuntime::Rhi::Vulkan::VulkanPipelineCache;
    void Expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    VkPhysicalDeviceProperties Identity()
    {
        VkPhysicalDeviceProperties id{}; id.vendorID = 0x10DE; id.deviceID = 7; id.driverVersion = 99;
        for (unsigned i = 0; i < VK_UUID_SIZE; ++i) id.pipelineCacheUUID[i] = static_cast<std::uint8_t>(3*i+1);
        return id;
    }
    std::vector<std::byte> Payload(const VkPhysicalDeviceProperties& id)
    {
        std::vector<std::byte> bytes(128);
        const std::array<std::uint32_t, 4> fields{32, VK_PIPELINE_CACHE_HEADER_VERSION_ONE, id.vendorID, id.deviceID};
        for (unsigned field = 0; field < fields.size(); ++field)
            for (unsigned byte = 0; byte < 4; ++byte)
                bytes[4*field+byte] = static_cast<std::byte>(fields[field] >> (8*byte));
        std::memcpy(bytes.data()+16, id.pipelineCacheUUID, VK_UUID_SIZE);
        for (unsigned i = 32; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(i*7);
        return bytes;
    }
    struct Driver
    {
        std::vector<std::byte> Blob = Payload(Identity());
        std::vector<std::size_t> InitialSizes;
        std::vector<VkPipelineCache> PipelineCaches;
        unsigned Destroyed = 0, Reads = 0, Incomplete = 0;
        bool RejectInitial = false, FailCreate = false, Oversized = false;
        VkResult PipelineResult = VK_SUCCESS;
    };
    Driver* Active = nullptr;
    VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice, const VkPipelineCacheCreateInfo* info,
        const VkAllocationCallbacks*, VkPipelineCache* cache)
    {
        Active->InitialSizes.push_back(info->initialDataSize);
        if (Active->FailCreate || (Active->RejectInitial && info->initialDataSize)) return VK_ERROR_OUT_OF_HOST_MEMORY;
        if (info->initialDataSize)
            Expect(info->initialDataSize == Active->Blob.size()
                && std::memcmp(info->pInitialData, Active->Blob.data(), Active->Blob.size()) == 0, "Driver received modified or unframed data.");
        *cache = reinterpret_cast<VkPipelineCache>(std::uintptr_t{17}); return VK_SUCCESS;
    }
    VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice, VkPipelineCache cache, const VkAllocationCallbacks*)
    { Expect(cache == reinterpret_cast<VkPipelineCache>(std::uintptr_t{17}), "Wrong cache destroyed."); ++Active->Destroyed; }
    VKAPI_ATTR VkResult VKAPI_CALL Data(VkDevice, VkPipelineCache, std::size_t* size, void* output)
    {
        ++Active->Reads;
        if (!output)
        {
            *size = Active->Oversized ? Cache::MaximumPayload+1 : Active->Incomplete ? 64 : Active->Blob.size();
            return VK_SUCCESS;
        }
        const auto copied = std::min(*size, Active->Blob.size());
        std::memcpy(output, Active->Blob.data(), copied); *size = copied;
        if (Active->Incomplete) { --Active->Incomplete; return VK_INCOMPLETE; }
        return VK_SUCCESS;
    }
    VKAPI_ATTR VkResult VKAPI_CALL Pipelines(VkDevice, VkPipelineCache cache, std::uint32_t count,
        const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline* pipeline)
    {
        Expect(count == 1, "Library changed pipeline count."); Active->PipelineCaches.push_back(cache);
        if (Active->PipelineResult == VK_SUCCESS) *pipeline = reinterpret_cast<VkPipeline>(std::uintptr_t{23});
        return Active->PipelineResult;
    }
    Cache::Dispatch Dispatch() { return {reinterpret_cast<VkDevice>(std::uintptr_t{5}), Create, Destroy, Data, Pipelines}; }
    void Compile(Cache& cache)
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        Expect(cache.CreatePipeline(info, pipeline) == VK_SUCCESS && pipeline != VK_NULL_HANDLE, "Pipeline compile failed.");
    }
    void Store(const std::filesystem::path& file, std::span<const std::byte> bytes)
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Expect(static_cast<bool>(stream), "Test file write failed.");
    }
    std::vector<std::byte> Load(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary | std::ios::ate);
        Expect(static_cast<bool>(stream), "Test file read failed.");
        std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg())); stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Expect(static_cast<bool>(stream), "Test file read incomplete."); return bytes;
    }
    void Framing()
    {
        const auto id = Identity(); const auto payload = Payload(id); const auto file = Cache::Frame(payload, id);
        Expect(Cache::Unframe(file, id) == payload, "Roundtrip changed driver bytes.");
        for (std::size_t length = 0; length < file.size(); ++length)
            Expect(Cache::Unframe(std::span(file).first(length), id).empty(), "Truncated frame accepted.");
        for (std::size_t byte = 0; byte < file.size(); ++byte)
        {
            auto corrupted = file; corrupted[byte] ^= std::byte{1};
            Expect(Cache::Unframe(corrupted, id).empty(), "Corrupt header or payload accepted.");
        }
        auto trailing = file; trailing.push_back(std::byte{0});
        Expect(Cache::Unframe(trailing, id).empty(), "Trailing bytes accepted.");
        auto oversized = file; for (unsigned byte = 0; byte < 8; ++byte) oversized[48+byte] = std::byte{255};
        Expect(Cache::Unframe(oversized, id).empty(), "Oversized advertised payload accepted.");
        auto changed = id; ++changed.vendorID; Expect(Cache::Unframe(file, changed).empty(), "Vendor mismatch accepted.");
        changed = id; ++changed.deviceID; Expect(Cache::Unframe(file, changed).empty(), "Device mismatch accepted.");
        changed = id; ++changed.driverVersion; Expect(Cache::Unframe(file, changed).empty(), "Driver mismatch accepted.");
        changed = id; changed.pipelineCacheUUID[7] ^= 1; Expect(Cache::Unframe(file, changed).empty(), "UUID mismatch accepted.");
        auto malformed = payload; malformed[0] = std::byte{31};
        Expect(Cache::Frame(malformed, id).empty(), "Malformed native header accepted for persistence.");
    }
    void Lifecycle(const std::filesystem::path& root)
    {
        const auto path = root / "cache.bin"; const auto id = Identity();
        Driver cold; Active = &cold; cold.Incomplete = 1;
        {
            Cache cache(Dispatch(), id, path); Expect(!cache.Stats().Loaded && cache.Stats().Native, "Cold cache state wrong.");
            Compile(cache); cache.Close(); cache.Close();
            Expect(cache.Stats().SavedBytes == cold.Blob.size() && cold.Reads == 4, "Incomplete retrieval was not retried.");
            bool rejected = false;
            try { Compile(cache); } catch (const std::logic_error&) { rejected = true; }
            Expect(rejected, "Closed library accepted a pipeline.");
        }
        Expect(cold.Destroyed == 1 && cold.InitialSizes == std::vector<std::size_t>{0}
            && cold.PipelineCaches[0] != VK_NULL_HANDLE, "Cold cache ownership or pipeline binding wrong.");
        const auto good = Load(path); Expect(Cache::Unframe(good, id) == cold.Blob, "Saved blob invalid.");
        Driver warm; Active = &warm;
        {
            Cache cache(Dispatch(), id, path); Expect(cache.Stats().LoadedBytes == warm.Blob.size(), "Warm data not loaded.");
            Compile(cache); cache.Close(); Expect(cache.Stats().CachedCreations == 1, "Native cache not used.");
        }
        Expect(warm.InitialSizes == std::vector<std::size_t>{warm.Blob.size()} && warm.Destroyed == 1, "Warm ownership wrong.");
        Driver rejection; rejection.RejectInitial = true; Active = &rejection;
        { Cache cache(Dispatch(), id, path); Expect(!cache.Stats().Loaded && cache.Stats().Native, "Rejected data not retried empty."); Compile(cache); }
        Expect(rejection.InitialSizes == std::vector<std::size_t>{rejection.Blob.size(), 0}, "Driver rejection retry wrong.");
        Driver unavailable; unavailable.FailCreate = true; Active = &unavailable;
        { Cache cache(Dispatch(), id, path); Expect(!cache.Stats().Native, "Failed native cache is live."); Compile(cache); }
        Expect(unavailable.PipelineCaches == std::vector<VkPipelineCache>{VK_NULL_HANDLE} && !unavailable.Destroyed,
            "Unavailable cache prevented uncached pipeline creation.");
        Driver missing; Active = &missing;
        {
            auto dispatch = Dispatch(); dispatch.Create = nullptr; dispatch.Destroy = nullptr; dispatch.Data = nullptr;
            Cache cache(dispatch, id, path); Compile(cache);
            Expect(cache.Stats().UncachedCreations == 1, "Missing optional entry points prevented compile.");
        }
        auto corrupt = good; corrupt.back() ^= std::byte{1}; Store(path, corrupt);
        Driver invalid; Active = &invalid;
        { Cache cache(Dispatch(), id, path); Expect(!cache.Stats().Loaded, "Corrupt file reached native create."); Compile(cache); }
        Expect(invalid.InitialSizes == std::vector<std::size_t>{0}, "Corrupt file handed to driver.");
        Store(path, good);
        Driver oversized; oversized.Oversized = true; Active = &oversized;
        { Cache cache(Dispatch(), id, path); Compile(cache); }
        Expect(oversized.Reads == 1 && Load(path) == good, "Oversized retrieval allocated or overwrote the cache.");
        const auto blocked = root / "blocked"; Store(blocked, std::array{std::byte{7}});
        Driver unwritable; Active = &unwritable;
        { Cache cache(Dispatch(), id, blocked / "cache.bin"); Compile(cache); }
        Expect(unwritable.Destroyed == 1 && Load(blocked) == std::vector<std::byte>{std::byte{7}}, "Save failure prevented teardown or damaged existing file.");
        Driver fault; fault.PipelineResult = VK_ERROR_DEVICE_LOST; Active = &fault;
        {
            Cache cache(Dispatch(), id); VkPipeline pipeline = VK_NULL_HANDLE;
            const VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            Expect(cache.CreatePipeline(info, pipeline) == VK_ERROR_DEVICE_LOST, "Library hid a real pipeline device loss.");
        }
        Driver concurrent; Active = &concurrent;
        {
            Cache cache(Dispatch(), id);
            std::array<std::exception_ptr, 2> failures{};
            auto compile = [&](unsigned slot) {
                try { for (unsigned i = 0; i < 64; ++i) Compile(cache); }
                catch (...) { failures[slot] = std::current_exception(); }
            };
            std::thread first(compile, 0), second(compile, 1); first.join(); second.join();
            for (const auto& failure : failures) if (failure) std::rethrow_exception(failure);
            Expect(cache.Stats().CachedCreations == 128 && concurrent.PipelineCaches.size() == 128,
                "Concurrent native cache calls were not serialized.");
        }
    }
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("fruity-vk-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        std::filesystem::create_directory(root); Framing(); Lifecycle(root);
        // Only the files this test created are removed; no recursive deletion.
        std::filesystem::remove(root / "cache.bin"); std::filesystem::remove(root / "blocked"); std::filesystem::remove(root);
        std::cout << "Vulkan pipeline library tests passed: framing, identity, corruption, bounded data, cold/warm, rejection, uncached creation, failure preservation, teardown.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << "; evidence in " << root << '\n'; return 1; }
}
