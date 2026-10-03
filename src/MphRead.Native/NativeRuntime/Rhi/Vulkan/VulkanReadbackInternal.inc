void VulkanCommandList::PrepareHostReadback(VulkanBuffer& buffer)
{
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer.Native(); barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1; dependency.pBufferMemoryBarriers = &barrier;
    _device->ContextPointer->_impl->vkCmdPipelineBarrier2(_commandSlots->Buffer(), &dependency);
}
bool VulkanCommandList::PollComplete()
{
    return _commandSlots->PollComplete();
}
class VulkanReadbackTransfer final : public ReadbackTransfer
{
public:
    std::unique_ptr<VulkanBuffer> Buffer;
    std::unique_ptr<VulkanCommandList> Commands;
    SubmissionSerial Serial;
    std::uint32_t OutputPixelBytes = 0;
    bool OpaqueAlpha = false;
    bool Bgra = false;
    bool IsReady() override
    { return Buffer->DeviceState()->Scheduler->Poll() >= Serial && (!Commands || Commands->PollComplete()); }
    void CopyResult(std::span<std::byte> destination) override
    {
        const auto& state = Buffer->DeviceState();
        state->RequireAlive();
        void* mapped = nullptr;
        Check(vmaMapMemory(state->Allocator, Buffer->Allocation(), &mapped), "vmaMapMemory(async readback)");
        struct Unmap final
        {
            VmaAllocator Allocator; VmaAllocation Allocation;
            ~Unmap() { vmaUnmapMemory(Allocator, Allocation); }
        } unmap{state->Allocator, Buffer->Allocation()};
        Check(vmaInvalidateAllocation(state->Allocator, Buffer->Allocation(), 0, Buffer->Desc().size),
            "vmaInvalidateAllocation(async readback)");
        const auto* source = static_cast<const std::byte*>(mapped);
        if (!OutputPixelBytes) std::memcpy(destination.data(), source, destination.size());
        else for (std::size_t i = 0, pixels = destination.size() / OutputPixelBytes; i < pixels; ++i)
        {
            std::memcpy(destination.data() + i * OutputPixelBytes, source + i * 4, OutputPixelBytes);
            if (Bgra) std::swap(destination[i * OutputPixelBytes], destination[i * OutputPixelBytes + 2]);
            if (OutputPixelBytes == 4 && OpaqueAlpha) destination[i * 4 + 3] = std::byte{255};
        }
    }
};
ReadbackTicket VulkanGraphicsDevice::EnqueueReadback(Buffer& source, std::uint64_t offset, std::uint64_t bytes)
{
    auto* native = dynamic_cast<VulkanBuffer*>(&source);
    if (!native || native->DeviceState() != _state) throw std::invalid_argument("Async source belongs to another device.");
    if (!bytes || offset > source.Desc().size || bytes > source.Desc().size - offset)
        throw std::out_of_range("Async buffer read range is outside the resource.");
    if (!Has(source.Desc().usage, BufferUsage::TransferSrc))
        throw std::invalid_argument("Async buffer readback requires TransferSrc usage.");
    return _state->Readbacks.Enqueue(bytes, bytes, [&]() -> std::unique_ptr<ReadbackTransfer> {
        auto result = std::make_unique<VulkanReadbackTransfer>();
        result->Buffer = std::make_unique<VulkanBuffer>(_state,
            BufferDesc{bytes, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
        result->Commands = std::make_unique<VulkanCommandList>(_state, true);
        auto& commands = *result->Commands;
        commands.Begin();
        const auto previous = native->State();
        if (previous != ResourceState::CopySrc) commands.Transition(source, previous, ResourceState::CopySrc);
        commands.Transition(*result->Buffer, ResourceState::Undefined, ResourceState::CopyDst);
        commands.CopyBuffer(source, offset, *result->Buffer, 0, bytes);
        commands.PrepareHostReadback(*result->Buffer);
        if (previous != ResourceState::Undefined && previous != ResourceState::CopySrc)
            commands.Transition(source, ResourceState::CopySrc, previous);
        commands.Flush(); // End() is the explicit synchronous compatibility API.
        result->Serial = _state->Scheduler->Submitted();
        return result;
    });
}
ReadbackTicket VulkanCommandList::EnqueueReadColor(const RenderingInfo& info, std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height, TextureFormat format)
{
    _device->RequireAlive();
    if (format != TextureFormat::RGB8Unorm && format != TextureFormat::RGBA8Unorm)
        throw std::invalid_argument("Async color readback requires RGB8 or RGBA8.");
    VulkanTexture* texture = info.swapchain ? _device->WindowColor.get() : nullptr;
    if (!info.swapchain && !info.colorAttachments.empty() && info.colorAttachments[0].view)
    {
        const auto& view = CheckedResource<VulkanTextureView>(*info.colorAttachments[0].view, _device);
        if (view.Desc().baseMipLevel || view.Desc().mipLevelCount != 1
            || view.Desc().baseArrayLayer || view.Desc().arrayLayerCount != 1)
            throw std::invalid_argument("Async color source requires a base mip/layer view.");
        texture = const_cast<VulkanTexture*>(&CheckedResource<VulkanTexture>(view.TextureResource(), _device));
    }
    if (!texture || texture->DeviceState() != _device) throw std::invalid_argument("Async color source is absent or foreign.");
    if (!Has(texture->Desc().usage, TextureUsage::TransferSrc)) throw std::invalid_argument("Async color source requires TransferSrc.");
    const auto sourceFormat = texture->Desc().format;
    if (sourceFormat != TextureFormat::RGB8Unorm && sourceFormat != TextureFormat::RGBA8Unorm
        && sourceFormat != TextureFormat::RGBA8Srgb && sourceFormat != TextureFormat::BGRA8Unorm
        && sourceFormat != TextureFormat::BGRA8Srgb)
        throw std::invalid_argument("Async color source requires an RGB8/RGBA8/BGRA8 image.");
    if (texture->Desc().sampleCount != 1) throw std::invalid_argument("Async color source must be resolved first.");
    if (x > info.width || width > info.width - x || y > info.height || height > info.height - y)
        throw std::out_of_range("Async color readback is outside the rendering extent.");
    if (x > texture->Desc().width || width > texture->Desc().width - x
        || y > texture->Desc().height || height > texture->Desc().height - y)
        throw std::out_of_range("Async color readback is outside the image extent.");
    const auto pixelBytes = format == TextureFormat::RGB8Unorm ? 3U : 4U;
    const auto outputBytes = ReadbackColorBytes(width, height, pixelBytes);
    const auto stagingBytes = ReadbackColorBytes(width, height, 4);
    // A burst of captures can consume both recording slots before the GPU
    // finishes either. Reject this request instead of BeginBuffer's normal
    // frame throttling wait. The next draw retains its existing pacing policy.
    if (!_recording && !_commandSlots->CanBeginWithoutWait()) return {};
    return _device->Readbacks.Enqueue(outputBytes, stagingBytes, [&]() -> std::unique_ptr<ReadbackTransfer> {
        auto result = std::make_unique<VulkanReadbackTransfer>();
        result->OutputPixelBytes = pixelBytes;
        result->OpaqueAlpha = texture->Desc().format == TextureFormat::RGB8Unorm;
        result->Bgra = sourceFormat == TextureFormat::BGRA8Unorm || sourceFormat == TextureFormat::BGRA8Srgb;
        result->Buffer = std::make_unique<VulkanBuffer>(_device,
            BufferDesc{stagingBytes, BufferUsage::TransferDst, MemoryUsage::GpuToCpu});
        if (!_recording) BeginBuffer();
        if (_renderingOpen && _clearsPending) Materialize();
        if (_renderingActive) EndNative();
        const auto previous = texture->State();
        Barrier(*texture, ResourceState::CopySrc);
        Transition(*result->Buffer, ResourceState::Undefined, ResourceState::CopyDst);
        BufferTextureCopy region{};
        region.x = x; region.y = y; region.width = width; region.height = height;
        // Output leases decode physical RGBA8 bytes, including alpha/BGRA
        // handling. Public RGB buffer copies pack three bytes on the GPU.
        CopyPhysicalTextureToBuffer(*texture, *result->Buffer, region);
        PrepareHostReadback(*result->Buffer);
        if (previous != ResourceState::Undefined) Barrier(*texture, previous);
        Flush();
        result->Serial = _device->Scheduler->Submitted();
        return result;
    });
}
