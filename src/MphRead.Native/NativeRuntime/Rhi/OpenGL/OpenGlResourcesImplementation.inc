std::unique_ptr<Buffer> OpenGlGraphicsDevice::CreateBuffer(const BufferDesc& desc)
{
    if (!desc.size || desc.usage == BufferUsage::None || !IsValidBufferState(desc, desc.initialState)
        || desc.size > static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()))
        throw std::invalid_argument("OpenGL RHI: invalid buffer size, usage or initial state.");
    AdmitStorage(desc.size);
    const auto name = CreateGeometryBuffer();
    if (!name) throw std::runtime_error("OpenGL returned buffer object zero.");
    try
    {
        GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F37), name);
        GL::BufferData(static_cast<GL::BufferTarget>(0x8F37), static_cast<std::size_t>(desc.size), nullptr,
            desc.memoryUsage == MemoryUsage::GpuOnly ? GL::BufferUsageHint::StaticDraw : GL::BufferUsageHint::StreamDraw);
        GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F37), 0);
        CheckStorageResult("OpenGL buffer allocation");
        auto buffer = std::make_unique<OpenGlBuffer>(*this, desc, name);
        Track(*buffer);
        return buffer;
    }
    catch (...) { Retire({GlObject::Kind::Buffer, name}); throw; }
}

OpenGlBuffer::~OpenGlBuffer()
{
    if (_device) { _device->Untrack(*this); _device->Retire({GlObject::Kind::Buffer, _name}); }
}

OpenGlBuffer& CheckedBuffer(OpenGlGraphicsDevice& device, Buffer& resource)
{
    auto* buffer = dynamic_cast<OpenGlBuffer*>(&resource);
    if (!buffer || buffer->Device() != &device) throw std::invalid_argument("OpenGL RHI: buffer belongs to another device.");
    return *buffer;
}
const OpenGlBuffer& CheckedBuffer(OpenGlGraphicsDevice& device, const Buffer& resource)
{
    return CheckedBuffer(device, const_cast<Buffer&>(resource));
}
void CheckBufferRange(const Buffer& resource, std::uint64_t offset, std::uint64_t size)
{
    if (!size || offset > resource.Desc().size || size > resource.Desc().size - offset)
        throw std::out_of_range("OpenGL RHI: buffer range is empty or outside the allocation.");
}
void OpenGlGraphicsDevice::WriteBuffer(Buffer& resource, std::uint64_t offset, std::span<const std::byte> data)
{
    auto& buffer = CheckedBuffer(*this, resource);
    CheckBufferRange(resource, offset, data.size());
    if (!offset && data.size() == buffer.Desc().size) AdmitStorage(data.size());
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F37), buffer.Name());
    if (!offset && data.size() == buffer.Desc().size)
        GL::BufferData(static_cast<GL::BufferTarget>(0x8F37), data.size(), data.data(),
            buffer.Desc().memoryUsage == MemoryUsage::GpuOnly ? GL::BufferUsageHint::StaticDraw : GL::BufferUsageHint::StreamDraw);
    else OpenGlNative::Require(_api.BufferSubData, "glBufferSubData")(0x8F37,
        static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(data.size()), data.data());
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F37), 0);
    CheckStorageResult("OpenGL buffer upload");
    // CPU uploads retain the state; GPU-only writes use the transfer path's
    // final state, as on Vulkan, without changing an established read state.
    if (buffer.Desc().memoryUsage == MemoryUsage::GpuOnly && buffer.State() == ResourceState::Undefined)
        buffer.State(ResourceState::CopyDst);
}
void OpenGlGraphicsDevice::ReadBuffer(Buffer& resource, std::uint64_t offset, std::span<std::byte> data)
{
    auto& buffer = CheckedBuffer(*this, resource);
    CheckBufferRange(resource, offset, data.size());
    if (resource.Desc().memoryUsage != MemoryUsage::GpuToCpu)
        throw std::invalid_argument("OpenGL RHI: readback requires a GpuToCpu buffer.");
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F36), buffer.Name());
    void* mapped = OpenGlNative::Require(_api.MapBufferRange, "glMapBufferRange")(0x8F36,
        static_cast<std::ptrdiff_t>(offset), static_cast<std::ptrdiff_t>(data.size()), 1);
    if (!mapped) { GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F36), 0); throw std::runtime_error("OpenGL buffer readback mapping failed."); }
    std::memcpy(data.data(), mapped, data.size());
    const bool valid = OpenGlNative::Require(_api.UnmapBuffer, "glUnmapBuffer")(0x8F36) != 0;
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F36), 0);
    if (!valid) throw std::runtime_error("OpenGL buffer readback was invalidated.");
}

OpenGlSamplerStorage::OpenGlSamplerStorage(OpenGlGraphicsDevice& device, SamplerDesc desc)
    : Device(&device), Desc(desc)
{
    if (!std::isfinite(desc.minLod) || !std::isfinite(desc.maxLod) || desc.maxLod < desc.minLod
        || !std::isfinite(desc.maxAnisotropy) || desc.maxAnisotropy < 1
        || desc.maxAnisotropy > device.GetCapabilities().maxSamplerAnisotropy)
        throw std::invalid_argument("OpenGL RHI: invalid sampler LOD or anisotropy.");
    auto& api = device.Api();
    OpenGlNative::Require(api.GenSamplers, "glGenSamplers")(1, &Name);
    if (!Name) throw std::runtime_error("OpenGL returned sampler object zero.");
    try
    {
        // Texture allocation sets its actual mip range. The sampler remains
        // independent, so one image can be sampled through different filters.
        const int minification = desc.minFilter == Filter::Nearest
            ? (desc.mipFilter == Filter::Nearest ? 0x2700 : 0x2702)
            : (desc.mipFilter == Filter::Nearest ? 0x2701 : 0x2703);
        auto integer = OpenGlNative::Require(api.SamplerParameteri, "glSamplerParameteri");
        auto scalar = OpenGlNative::Require(api.SamplerParameterf, "glSamplerParameterf");
        integer(Name, 0x2801, minification);
        integer(Name, 0x2800, ToGl(desc.magFilter, false));
        integer(Name, 0x2802, ToGl(desc.addressU)); integer(Name, 0x2803, ToGl(desc.addressV)); integer(Name, 0x8072, ToGl(desc.addressW));
        scalar(Name, 0x813A, desc.minLod); scalar(Name, 0x813B, desc.maxLod);
#if !defined(__ANDROID__)
        const std::array<float, 4> border = desc.borderColor == BorderColor::OpaqueWhite
            ? std::array<float, 4>{1, 1, 1, 1} : desc.borderColor == BorderColor::OpaqueBlack
                ? std::array<float, 4>{0, 0, 0, 1} : std::array<float, 4>{0, 0, 0, 0};
        OpenGlNative::Require(api.SamplerParameterfv, "glSamplerParameterfv")(Name, 0x1004, border.data());
        if (device.GetCapabilities().supportsAnisotropy) scalar(Name, 0x84FE, desc.maxAnisotropy);
#endif
        const int error = static_cast<int>(GL::GetError());
        if (error) throw std::runtime_error("OpenGL sampler creation failed: " + std::to_string(error));
        device.Track(*this);
    }
    catch (...) { api.DeleteSamplers(1, &Name); throw; }
}
OpenGlSamplerStorage::~OpenGlSamplerStorage()
{
    if (Device)
    {
        Device->Untrack(*this);
        Device->Retire({GlObject::Kind::Sampler, static_cast<int>(Name), Device->Api().DeleteSamplers});
    }
}

const OpenGlBindingLayout& CheckedLayout(OpenGlGraphicsDevice& device, const BindingLayout* value)
{
    const auto* layout = dynamic_cast<const OpenGlBindingLayout*>(value);
    if (!layout || layout->Device() != &device) throw std::invalid_argument("OpenGL RHI: invalid binding layout.");
    return *layout;
}
OpenGlBindingSet::OpenGlBindingSet(OpenGlGraphicsDevice& device, const BindingSetDesc& desc)
    : _device(&device), _lifetime(DeviceLifetime(device)), _layout(device, CheckedLayout(device, desc.layout).Desc()), _desc(desc)
{
    _desc.layout = &_layout;
    std::uint64_t count = 0;
    for (const auto& declaration : _layout.Desc().entries) count += declaration.count;
    if (count != _desc.entries.size()) throw std::invalid_argument("OpenGL RHI: every binding array element must be initialized.");
    for (std::size_t i = 0; i < _desc.entries.size(); ++i)
    {
        const auto& entry = _desc.entries[i];
        const auto& declarations = _layout.Desc().entries;
        const auto found = std::find_if(declarations.begin(), declarations.end(), [&](const auto& value) { return value.binding == entry.binding; });
        if (found == declarations.end() || entry.arrayElement >= found->count) throw std::invalid_argument("OpenGL RHI: undeclared binding array element.");
        for (std::size_t j = 0; j < i; ++j)
            if (_desc.entries[j].binding == entry.binding && _desc.entries[j].arrayElement == entry.arrayElement)
                throw std::invalid_argument("OpenGL RHI: duplicate binding array element.");
        if (found->type == BindingType::UniformBuffer || found->type == BindingType::StorageBuffer)
        {
            const auto* value = std::get_if<BufferBinding>(&entry.resource);
            if (!value || !value->buffer) throw std::invalid_argument("OpenGL RHI: wrong buffer binding type.");
            const auto& buffer = CheckedBuffer(device, *value->buffer);
            const bool uniform = found->type == BindingType::UniformBuffer;
            const auto alignment = GL::GetInteger(uniform ? 0x8A34 : 0x90DF);
            const auto size = value->size ? value->size : buffer.Desc().size - value->offset;
            CheckBufferRange(buffer, value->offset, size);
            if (!Has(buffer.Desc().usage, uniform ? BufferUsage::Uniform : BufferUsage::Storage)
                || alignment <= 0 || value->offset % static_cast<std::uint64_t>(alignment))
                throw std::invalid_argument("OpenGL RHI: invalid buffer binding usage or alignment.");
        }
        else if (found->type == BindingType::Sampler)
        {
            const auto* value = std::get_if<SamplerBinding>(&entry.resource);
            const auto* sampler = value ? dynamic_cast<const OpenGlSampler*>(value->sampler) : nullptr;
            if (!sampler || sampler->Device() != &device) throw std::invalid_argument("OpenGL RHI: invalid sampler binding.");
        }
        else
        {
            const auto* value = std::get_if<TextureBinding>(&entry.resource);
            const auto* view = value ? dynamic_cast<const OpenGlTextureView*>(value->view) : nullptr;
            const auto* texture = view && view->TextureAlive() ? dynamic_cast<const OpenGlTexture*>(&view->TextureResource()) : nullptr;
            if (!texture || texture->IsRenderbuffer()
                || device.FindTexture(texture->Handle()) != texture
                || !Has(texture->Desc().usage, found->type == BindingType::StorageTexture ? TextureUsage::Storage : TextureUsage::Sampled))
                throw std::invalid_argument("OpenGL RHI: invalid texture binding.");
        }
        _resourceLifetimes.push_back(std::visit([](const auto& resource) -> std::weak_ptr<void> {
            using T = std::decay_t<decltype(resource)>;
            if constexpr (std::is_same_v<T, BufferBinding>)
                return static_cast<const OpenGlBuffer*>(resource.buffer)->Lifetime();
            else if constexpr (std::is_same_v<T, TextureBinding>)
                return static_cast<const OpenGlTextureView*>(resource.view)->Lifetime();
            else return static_cast<const OpenGlSampler*>(resource.sampler)->Lifetime();
        }, entry.resource));
    }
}

void OpenGlBindingSet::ValidateResources() const
{
    if (_lifetime.expired()) throw std::invalid_argument("OpenGL RHI: binding set session has ended.");
    for (const auto& lifetime : _resourceLifetimes)
        if (lifetime.expired()) throw std::invalid_argument("OpenGL RHI: binding resource has expired.");
    for (const auto& entry : _desc.entries)
    {
        if (const auto* buffer = std::get_if<BufferBinding>(&entry.resource))
        {
            if (!buffer->buffer) throw std::invalid_argument("OpenGL RHI: empty buffer binding.");
            (void)CheckedBuffer(*_device, *buffer->buffer);
        }
        else if (const auto* image = std::get_if<TextureBinding>(&entry.resource))
        {
            if (!image->view) throw std::invalid_argument("OpenGL RHI: empty texture binding.");
            (void)ViewTexture(*image->view, _device);
        }
        else if (const auto* value = std::get_if<SamplerBinding>(&entry.resource))
        {
            const auto* sampler = dynamic_cast<const OpenGlSampler*>(value->sampler);
            if (!sampler || sampler->Device() != _device) throw std::invalid_argument("OpenGL RHI: foreign sampler binding.");
        }
    }
}
