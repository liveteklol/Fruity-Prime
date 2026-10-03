class OpenGlReadbackTransfer final : public ReadbackTransfer
{
public:
    OpenGlReadbackTransfer(OpenGlGraphicsDevice& device, std::unique_ptr<Buffer> buffer, SubmissionSerial serial)
        : _device(device), _buffer(std::move(buffer)), _serial(serial) {}
    bool IsReady() override { return _device.ReadbackReady(_serial); }
    void CopyResult(std::span<std::byte> destination) override
    {
        const auto previous = GL::GetInteger(0x8F36); // COPY_READ_BUFFER_BINDING
        struct Restore final
        {
            int Name;
            ~Restore() { GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F36), Name); }
        } restore{previous};
        _device.ReadBuffer(*_buffer, 0, destination);
    }
private:
    OpenGlGraphicsDevice& _device;
    std::unique_ptr<Buffer> _buffer;
    SubmissionSerial _serial;
};
ReadbackTicket OpenGlGraphicsDevice::EnqueueBytes(std::uint64_t bytes, const std::function<void(Buffer&)>& copy)
{
    if (!SupportsAsyncReadback()) throw std::logic_error("OpenGL asynchronous readback is unavailable.");
    return _readbacks.Enqueue(bytes, bytes, [&]() -> std::unique_ptr<ReadbackTransfer> {
        auto target = CreateBuffer({bytes, BufferUsage::TransferDst, MemoryUsage::GpuToCpu, ResourceState::CopyDst});
        copy(*target);
        const auto serial = _scheduler.Submit(true);
        return std::make_unique<OpenGlReadbackTransfer>(*this, std::move(target), serial);
    });
}
ReadbackTicket OpenGlGraphicsDevice::EnqueueReadback(Buffer& source, std::uint64_t offset, std::uint64_t bytes)
{
    auto& native = CheckedBuffer(*this, source);
    CheckBufferRange(source, offset, bytes);
    if (!Has(source.Desc().usage, BufferUsage::TransferSrc))
        throw std::invalid_argument("Async buffer readback requires TransferSrc usage.");
    return EnqueueBytes(bytes, [&](Buffer& target) {
        auto commands = CreateCommandList();
        commands->Begin();
        const auto previous = native.State();
        if (previous != ResourceState::CopySrc) commands->Transition(source, previous, ResourceState::CopySrc);
        commands->CopyBuffer(source, offset, target, 0, bytes);
        if (previous != ResourceState::Undefined && previous != ResourceState::CopySrc)
            commands->Transition(source, ResourceState::CopySrc, previous);
        // EnqueueBytes submits this context stream once the copy returns.
    });
}
