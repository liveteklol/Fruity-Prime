class VulkanTimestampSet final : public TimestampQuerySet, public VulkanNativeOwner
{
public:
    VulkanTimestampSet(std::shared_ptr<VulkanDeviceState> device, std::uint32_t count,
        std::string label, std::shared_ptr<void> reservation)
        : Device(std::move(device)), Writes(count), _registration(*Device, *this),
          _properties(Device->ContextPointer->_impl->timestampProperties), _reservation(std::move(reservation))
    {
        auto& vk = *Device->ContextPointer->_impl;
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP; info.queryCount = count;
        Check(vk.vkCreateQueryPool(vk.device, &info, nullptr, &Pool), "vkCreateQueryPool(timestamp)");
        try { vk.Name(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<std::uint64_t>(Pool), label.c_str()); }
        catch (...) { vk.vkDestroyQueryPool(vk.device, Pool, nullptr); Pool = VK_NULL_HANDLE; throw; }
    }
    ~VulkanTimestampSet() override { CloseNative(); }
    void CloseNative() noexcept override
    {
        if (!Pool) return;
        if (!Device->Closing) { try { Device->FlushScene(); } catch (...) {} }
        auto& vk = *Device->ContextPointer->_impl;
        const auto destroy = vk.vkDestroyQueryPool; const auto device = vk.device;
        const auto pool = std::exchange(Pool, VK_NULL_HANDLE);
        auto reservation = std::move(_reservation);
        Device->Retire([destroy, device, pool, reservation = std::move(reservation)] { destroy(device, pool, nullptr); });
    }
    std::uint32_t Count() const noexcept override { return Writes.Count(); }
    TimestampProperties Properties() const noexcept override { return _properties; }
    TimestampStatus ReadResults(std::span<std::uint64_t> destination) override
    {
        if (destination.size() != Count()) throw std::invalid_argument("GPU timestamp destination count differs.");
        if (!Pool) return TimestampStatus::Cancelled;
        if (!Writes.AllWritten()) return TimestampStatus::Pending;
        auto& vk = *Device->ContextPointer->_impl;
        struct Result final { std::uint64_t Value, Available; };
        std::array<Result, 64> results{};
        const auto result = vk.vkGetQueryPoolResults(vk.device, Pool, 0, Count(), Count() * sizeof(Result),
            results.data(), sizeof(Result), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result == VK_NOT_READY) return TimestampStatus::Pending;
        Check(result, "vkGetQueryPoolResults(timestamp)");
        for (std::uint32_t i = 0; i < Count(); ++i) if (!results[i].Available) return TimestampStatus::Pending;
        for (std::uint32_t i = 0; i < Count(); ++i) destination[i] = results[i].Value;
        return TimestampStatus::Ready;
    }
    std::shared_ptr<VulkanDeviceState> Device;
    TimestampWriteState Writes;
    VkQueryPool Pool = VK_NULL_HANDLE;
private:
    VulkanNativeRegistration _registration;
    TimestampProperties _properties;
    std::shared_ptr<void> _reservation;
};
