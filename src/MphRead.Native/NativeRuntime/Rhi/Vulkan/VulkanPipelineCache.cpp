#include "VulkanPipelineCache.hpp"

#if defined(FRUITY_HAS_VULKAN)
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    namespace
    {
        constexpr std::array<std::byte, 8> Magic{std::byte{'F'}, std::byte{'P'}, std::byte{'V'}, std::byte{'K'},
            std::byte{'P'}, std::byte{'C'}, std::byte{'0'}, std::byte{'1'}};
        constexpr std::size_t HeaderSize = 64;
        std::uint64_t Read(std::span<const std::byte> bytes, std::size_t offset, unsigned count)
        {
            std::uint64_t value = 0;
            for (unsigned i = 0; i < count; ++i)
                value |= std::to_integer<std::uint64_t>(bytes[offset+i]) << (8*i);
            return value;
        }
        void Write(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value, unsigned count)
        {
            for (unsigned i = 0; i < count; ++i) bytes[offset+i] = static_cast<std::byte>(value >> (8*i));
        }
        std::uint64_t Checksum(std::span<const std::byte> bytes)
        {
            std::uint64_t hash = 14695981039346656037ULL;
            for (const auto byte : bytes) { hash ^= std::to_integer<unsigned>(byte); hash *= 1099511628211ULL; }
            return hash;
        }
        bool NativeHeader(std::span<const std::byte> payload, const VkPhysicalDeviceProperties& identity)
        {
            return payload.size() >= 32 && payload.size() <= VulkanPipelineCache::MaximumPayload
                && Read(payload, 0, 4) == 32 && Read(payload, 4, 4) == VK_PIPELINE_CACHE_HEADER_VERSION_ONE
                && Read(payload, 8, 4) == identity.vendorID && Read(payload, 12, 4) == identity.deviceID
                && std::memcmp(payload.data()+16, identity.pipelineCacheUUID, VK_UUID_SIZE) == 0;
        }
        std::vector<std::byte> Load(const std::filesystem::path& file)
        {
            std::ifstream input(file, std::ios::binary | std::ios::ate);
            if (!input) return {};
            const auto length = input.tellg();
            if (length < HeaderSize || length > HeaderSize + VulkanPipelineCache::MaximumPayload) return {};
            std::vector<std::byte> bytes(static_cast<std::size_t>(length));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
            return bytes;
        }
        void Store(const std::filesystem::path& file, std::span<const std::byte> bytes)
        {
            // Each process writes its own temporary file; replacement is atomic.
            // Concurrent writers may lose cache entries, never a complete blob.
            static std::atomic<std::uint64_t> sequence{0};
#if defined(_WIN32)
            const auto pid = GetCurrentProcessId();
#else
            const auto pid = getpid();
#endif
            auto temporary = file;
            temporary += ".tmp-" + std::to_string(pid) + "-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++sequence);
            try
            {
                if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
                    throw std::runtime_error("write failed");
                output.close();
                if (!output) throw std::runtime_error("close failed");
#if defined(_WIN32)
                if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("replace failed");
#else
                std::filesystem::rename(temporary, file);
#endif
            }
            catch (...)
            {
                std::error_code ignored; std::filesystem::remove(temporary, ignored);
                throw;
            }
        }
    }

    std::vector<std::byte> VulkanPipelineCache::Frame(std::span<const std::byte> payload,
        const VkPhysicalDeviceProperties& identity)
    {
        if (!NativeHeader(payload, identity)) return {};
        std::vector<std::byte> bytes(HeaderSize + payload.size());
        std::copy(Magic.begin(), Magic.end(), bytes.begin());
        Write(bytes, 8, 1, 4); Write(bytes, 12, HeaderSize, 4);
        Write(bytes, 16, identity.vendorID, 4); Write(bytes, 20, identity.deviceID, 4);
        Write(bytes, 24, identity.driverVersion, 4); Write(bytes, 28, sizeof(void*), 4);
        std::memcpy(bytes.data()+32, identity.pipelineCacheUUID, VK_UUID_SIZE);
        Write(bytes, 48, payload.size(), 8); Write(bytes, 56, Checksum(payload), 8);
        std::copy(payload.begin(), payload.end(), bytes.begin()+HeaderSize);
        return bytes;
    }
    std::vector<std::byte> VulkanPipelineCache::Unframe(std::span<const std::byte> bytes,
        const VkPhysicalDeviceProperties& identity)
    {
        if (bytes.size() < HeaderSize || !std::equal(Magic.begin(), Magic.end(), bytes.begin())
            || Read(bytes, 8, 4) != 1 || Read(bytes, 12, 4) != HeaderSize
            || Read(bytes, 16, 4) != identity.vendorID || Read(bytes, 20, 4) != identity.deviceID
            || Read(bytes, 24, 4) != identity.driverVersion || Read(bytes, 28, 4) != sizeof(void*)
            || std::memcmp(bytes.data()+32, identity.pipelineCacheUUID, VK_UUID_SIZE) != 0
            || Read(bytes, 48, 8) > MaximumPayload || Read(bytes, 48, 8) != bytes.size()-HeaderSize) return {};
        auto payload = bytes.subspan(HeaderSize);
        if (!NativeHeader(payload, identity) || Read(bytes, 56, 8) != Checksum(payload)) return {};
        return {payload.begin(), payload.end()};
    }
    VulkanPipelineCache::VulkanPipelineCache(Dispatch dispatch, const VkPhysicalDeviceProperties& identity,
        std::filesystem::path file) : _dispatch(dispatch), _identity(identity), _file(std::move(file))
    {
        if (!_dispatch.Pipelines) throw std::invalid_argument("Pipeline library needs a native compiler.");
        if (!_dispatch.Create || !_dispatch.Destroy || !_dispatch.Data)
        {
            std::cerr << "[vulkan cache] optional entry points unavailable; continuing uncached\n";
            return;
        }
        std::vector<std::byte> payload;
        try { if (!_file.empty()) payload = Unframe(Load(_file), identity); }
        catch (...) { std::cerr << "[vulkan cache] cannot read cache; starting empty\n"; }
        VkPipelineCacheCreateInfo create{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        create.initialDataSize = payload.size(); create.pInitialData = payload.empty() ? nullptr : payload.data();
        auto result = _dispatch.Create(_dispatch.Device, &create, nullptr, &_cache);
        if (result != VK_SUCCESS && !payload.empty())
        {
            std::cerr << "[vulkan cache] initial data rejected (" << result << "); retrying empty\n";
            create.initialDataSize = 0; create.pInitialData = nullptr; _cache = VK_NULL_HANDLE;
            result = _dispatch.Create(_dispatch.Device, &create, nullptr, &_cache);
            payload.clear();
        }
        if (result != VK_SUCCESS)
        {
            _cache = VK_NULL_HANDLE;
            std::cerr << "[vulkan cache] creation failed (" << result << "); continuing uncached\n";
        }
        _stats.Native = _cache != VK_NULL_HANDLE;
        _stats.Loaded = _stats.Native && !payload.empty();
        _stats.LoadedBytes = _stats.Loaded ? payload.size() : 0;
        std::cout << "[vulkan cache] native=" << _stats.Native << " initial-bytes=" << _stats.LoadedBytes << '\n';
    }
    VkResult VulkanPipelineCache::CreatePipeline(const VkGraphicsPipelineCreateInfo& info, VkPipeline& pipeline)
    {
        std::lock_guard lock(_mutex);
        if (_closed) throw std::logic_error("Vulkan pipeline library's session has ended.");
        if (_cache) ++_stats.CachedCreations; else ++_stats.UncachedCreations;
        const auto start = std::chrono::steady_clock::now();
        const VkResult result = _dispatch.Pipelines(_dispatch.Device, _cache, 1, &info, nullptr, &pipeline);
        _stats.CreationNanoseconds += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        return result;
    }
    void VulkanPipelineCache::Save()
    {
        if (!_cache || _file.empty() || !_stats.CachedCreations) return;
        for (unsigned attempt = 0; attempt < 3; ++attempt)
        {
            std::size_t size = 0;
            if (_dispatch.Data(_dispatch.Device, _cache, &size, nullptr) != VK_SUCCESS || size < 32 || size > MaximumPayload) return;
            std::vector<std::byte> payload(size);
            const auto result = _dispatch.Data(_dispatch.Device, _cache, &size, payload.data());
            if (result == VK_INCOMPLETE) continue;
            if (result != VK_SUCCESS || size > payload.size()) return;
            payload.resize(size);
            auto bytes = Frame(payload, _identity);
            if (bytes.empty()) return;
            Store(_file, bytes); _stats.SavedBytes = payload.size();
            return;
        }
    }
    void VulkanPipelineCache::Close() noexcept
    {
        std::lock_guard lock(_mutex);
        if (_closed) return;
        try { Save(); } catch (...) { std::cerr << "[vulkan cache] save failed; keeping previous file\n"; }
        if (_cache) _dispatch.Destroy(_dispatch.Device, _cache, nullptr);
        _cache = VK_NULL_HANDLE; _stats.Native = false; _closed = true;
        std::cout << "[vulkan cache] cached-creations=" << _stats.CachedCreations << " uncached-creations="
            << _stats.UncachedCreations << " saved-bytes=" << _stats.SavedBytes
            << " create-ms=" << (_stats.CreationNanoseconds / 1000000.0) << '\n';
    }
    VulkanPipelineCache::Statistics VulkanPipelineCache::Stats() const
    { std::lock_guard lock(_mutex); return _stats; }
}
#endif
