unsigned ToGlTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::PointList: return 0;
    case PrimitiveTopology::LineList: return 1;
    case PrimitiveTopology::LineStrip: return 3;
    case PrimitiveTopology::TriangleList: return 4;
    case PrimitiveTopology::TriangleStrip: return 5;
    }
    throw std::invalid_argument("OpenGL RHI: unsupported primitive topology.");
}
struct GlAttributeFormat { int Components; unsigned Type; unsigned Bytes; bool Normalized; bool Integer; };
GlAttributeFormat AttributeFormat(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::Float: return {1, 0x1406, 4, false, false};
    case VertexFormat::Float2: return {2, 0x1406, 8, false, false};
    case VertexFormat::Float3: return {3, 0x1406, 12, false, false};
    case VertexFormat::Float4: return {4, 0x1406, 16, false, false};
    case VertexFormat::UByte4Norm: return {4, 0x1401, 4, true, false};
    case VertexFormat::Short2Norm: return {2, 0x1402, 4, true, false};
    case VertexFormat::Short4Norm: return {4, 0x1402, 8, true, false};
    case VertexFormat::UInt: return {1, 0x1405, 4, false, true};
    }
    throw std::invalid_argument("OpenGL RHI: unsupported vertex format.");
}
void OpenGlCommandList::SetVertexBuffer(std::uint32_t slot, const Buffer& resource, std::uint64_t offset)
{
    RequireRecording();
    const auto& buffer = CheckedBuffer(*_device, resource);
    if (slot >= _device->GetCapabilities().maxVertexBuffers || !Has(buffer.Desc().usage, BufferUsage::Vertex)
        || offset >= buffer.Desc().size) throw std::invalid_argument("OpenGL RHI: invalid vertex buffer binding.");
    _vertexBindings[slot] = {&buffer, offset};
}
void OpenGlCommandList::SetIndexBuffer(const Buffer& resource, IndexType type, std::uint64_t offset)
{
    RequireRecording();
    const auto& buffer = CheckedBuffer(*_device, resource);
    const unsigned width = type == IndexType::UInt16 ? 2 : 4;
    if (!Has(buffer.Desc().usage, BufferUsage::Index) || offset >= buffer.Desc().size || offset % width)
        throw std::invalid_argument("OpenGL RHI: invalid index buffer binding.");
    _indexBuffer = &buffer; _indexOffset = offset; _indexType = type;
}
unsigned OpenGlCommandList::VertexArray()
{
    if (!_applied && !_sceneGeometry) throw std::logic_error("OpenGL RHI: draw needs a pipeline.");
    // The scene compatibility adapter provides all vertex input explicitly;
    // its shader program can already be bound by SceneShaderSet::Use before
    // the next pass pipeline is set (notably during GPU resource rebuild).
    const GraphicsPipelineDesc empty{};
    const auto& desc = _applied ? _applied->Desc() : empty;
    const auto buffers = _sceneGeometry ? _sceneBuffers : std::span<const VertexBufferLayoutDesc>(desc.vertexBuffers);
    const auto attributes = _sceneGeometry ? _sceneAttributes : std::span<const VertexAttributeDesc>(desc.vertexAttributes);
    std::vector<std::uint64_t> key{buffers.size(), attributes.size()};
    std::vector<int> names;
    for (const auto& layout : buffers)
    {
        const auto found = _vertexBindings.find(layout.slot);
        if (found == _vertexBindings.end() || !layout.stride || layout.stride > INT32_MAX)
            throw std::invalid_argument("OpenGL RHI: missing or invalid vertex buffer layout.");
        const auto& binding = found->second;
        key.insert(key.end(), {layout.slot, layout.stride, static_cast<unsigned>(layout.inputRate),
            static_cast<unsigned>(binding.Buffer->Name()), binding.Offset});
        names.push_back(binding.Buffer->Name());
    }
    std::unordered_set<std::uint32_t> locations;
    for (const auto& attr : attributes)
    {
        const auto found = std::find_if(buffers.begin(), buffers.end(), [&](const auto& value) { return value.slot == attr.bufferSlot; });
        const auto format = AttributeFormat(attr.format);
        if (found == buffers.end() || attr.location >= _device->GetCapabilities().maxVertexBuffers
            || !locations.insert(attr.location).second || attr.offset > found->stride || format.Bytes > found->stride - attr.offset)
            throw std::invalid_argument("OpenGL RHI: invalid vertex attribute layout.");
        const auto& binding = _vertexBindings.at(attr.bufferSlot);
        CheckBufferRange(*binding.Buffer, binding.Offset + attr.offset, format.Bytes);
        key.insert(key.end(), {attr.location, attr.bufferSlot, static_cast<unsigned>(attr.format), attr.offset});
    }
    key.push_back(_indexBuffer ? static_cast<unsigned>(_indexBuffer->Name()) : 0);
    if (_indexBuffer) names.push_back(_indexBuffer->Name());
    if (const auto found = _vertexArrays.find(key); found != _vertexArrays.end()) return found->second.Name;
    auto& api = _device->Api();
    unsigned vao = 0;
    OpenGlNative::Require(api.GenVertexArrays, "glGenVertexArrays")(1, &vao);
    if (!vao) throw std::runtime_error("OpenGL returned vertex array zero.");
    try
    {
        OpenGlNative::Require(api.BindVertexArray, "glBindVertexArray")(vao);
        for (const auto& attr : attributes)
        {
            const auto layout = std::find_if(buffers.begin(), buffers.end(), [&](const auto& value) { return value.slot == attr.bufferSlot; });
            const auto& binding = _vertexBindings.at(attr.bufferSlot);
            const auto format = AttributeFormat(attr.format);
            GL::BindBuffer(GL::BufferTarget::ArrayBuffer, binding.Buffer->Name());
            const auto offset = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(binding.Offset + attr.offset));
            if (format.Integer) OpenGlNative::Require(api.VertexAttribIPointer, "glVertexAttribIPointer")(attr.location, format.Components, format.Type, static_cast<int>(layout->stride), offset);
            else OpenGlNative::Require(api.VertexAttribPointer, "glVertexAttribPointer")(attr.location, format.Components, format.Type, format.Normalized ? 1 : 0, static_cast<int>(layout->stride), offset);
            OpenGlNative::Require(api.EnableVertexAttribArray, "glEnableVertexAttribArray")(attr.location);
            OpenGlNative::Require(api.VertexAttribDivisor, "glVertexAttribDivisor")(attr.location, layout->inputRate == VertexInputRate::Instance ? 1 : 0);
        }
        GL::BindBuffer(GL::BufferTarget::ElementArrayBuffer, _indexBuffer ? _indexBuffer->Name() : 0);
        api.BindVertexArray(0);
        GL::BindBuffer(GL::BufferTarget::ArrayBuffer, 0);
        _vertexArrays.emplace(std::move(key), VertexArrayEntry{vao, std::move(names)});
        return vao;
    }
    catch (...) { api.BindVertexArray(0); api.DeleteVertexArrays(1, &vao); throw; }
}
void OpenGlCommandList::RestoreDrawState()
{
    if (!_applied && !_sceneGeometry) throw std::logic_error("OpenGL RHI: draw needs a pipeline.");
    for (const auto& [group, set] : _genericSets) set->ValidateResources();
    for (const auto& [group, set] : _genericSets)
        for (const auto& entry : set->Desc().entries)
        {
            const auto& declarations = set->Desc().layout->Desc().entries;
            const auto declaration = std::find_if(declarations.begin(), declarations.end(),
                [&](const auto& item) { return item.binding == entry.binding; });
            if (declaration->type != BindingType::SampledTexture) continue;
            const auto& texture = ViewTexture(*std::get<TextureBinding>(entry.resource).view, _device);
            if (texture.State() != ResourceState::Common && !HasAny(texture.State(), ResourceState::ShaderRead))
                throw std::invalid_argument("OpenGL RHI: sampled draw requires shader-readable texture state.");
        }
    for (const auto& unit : _units)
        if ((unit.Texture && unit.TextureLifetime.expired()) || (unit.Sampler && unit.SamplerLifetime.expired()))
            throw std::invalid_argument("OpenGL RHI: sampled binding resource has expired.");
    Touch();
    if (!_nativeDirty) return;
    const auto framebuffer = _framebuffers.find(_current);
    if ((_current.Color || _current.Depth) && framebuffer == _framebuffers.end())
        throw std::logic_error("OpenGL RHI: rendering target has expired.");
    GL::BindFramebuffer(GL::FramebufferTarget::Framebuffer, framebuffer == _framebuffers.end() ? 0 : framebuffer->second);
    if (_applied) SetPipeline(*_applied);
    SetViewport(_hasViewport ? _viewport : Viewport{0, 0, static_cast<float>(_renderWidth), static_cast<float>(_renderHeight)});
    if (_scissorEnabled) SetScissor(_scissor);
    else SetCap(0x0C11, false);
    for (const auto& [group, set] : _genericSets) ApplyBindingSet(group, *set);
    for (std::uint32_t slot = 0; slot < _units.size(); ++slot)
    {
        const auto unit = _units[slot];
        if (!unit.Bound) continue;
        if (!unit.Texture && unit.Sampler) BindInteropTexture(unit.InteropTexture, *unit.Sampler);
        else BindSampledTexture(slot, unit.Texture, unit.Sampler);
    }
    _nativeDirty = false;
}
void OpenGlCommandList::ForgetBuffer(std::int32_t name)
{
    RequireAlive();
    for (auto it = _vertexArrays.begin(); it != _vertexArrays.end();)
    {
        if (std::find(it->second.Buffers.begin(), it->second.Buffers.end(), name) != it->second.Buffers.end())
        {
            _device->Retire({GlObject::Kind::VertexArray, static_cast<int>(it->second.Name), _device->Api().DeleteVertexArrays});
            it = _vertexArrays.erase(it);
        }
        else ++it;
    }
    for (auto it = _vertexBindings.begin(); it != _vertexBindings.end();)
        if (it->second.Buffer->Name() == name) it = _vertexBindings.erase(it); else ++it;
    if (_indexBuffer && _indexBuffer->Name() == name) _indexBuffer = nullptr;
}
void OpenGlGraphicsDevice::ForgetBuffer(std::int32_t name)
{
    for (auto* list : _lists) list->ForgetBuffer(name);
}
void OpenGlCommandList::Draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::uint32_t firstInstance)
{
    RequireRendering();
    if (!count || !instances) return;
    RestoreDrawState();
    if (count > INT32_MAX || instances > INT32_MAX || first > INT32_MAX) throw std::out_of_range("OpenGL RHI: draw exceeds native count range.");
    auto& api = _device->Api();
    const auto vao = VertexArray();
    api.BindVertexArray(vao);
    const auto topology = ToGlTopology(_sceneGeometry ? _sceneTopology : _applied->Desc().topology);
    if (firstInstance) OpenGlNative::Require(api.DrawArraysInstancedBaseInstance, "glDrawArraysInstancedBaseInstance")(topology, first, count, instances, firstInstance);
    else OpenGlNative::Require(api.DrawArraysInstanced, "glDrawArraysInstanced")(topology, first, count, instances);
    api.BindVertexArray(0);
}
void OpenGlCommandList::DrawIndexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base, std::uint32_t firstInstance)
{
    RequireRendering();
    if (!count || !instances) return;
    RestoreDrawState();
    if (!_indexBuffer) throw std::logic_error("OpenGL RHI: indexed draw needs an index buffer.");
    if (count > INT32_MAX || instances > INT32_MAX) throw std::out_of_range("OpenGL RHI: draw exceeds native count range.");
    const unsigned width = _indexType == IndexType::UInt16 ? 2 : 4;
    const auto offset = _indexOffset + static_cast<std::uint64_t>(first) * width;
    CheckBufferRange(*_indexBuffer, offset, static_cast<std::uint64_t>(count) * width);
    auto& api = _device->Api();
    api.BindVertexArray(VertexArray());
    const auto topology = ToGlTopology(_sceneGeometry ? _sceneTopology : _applied->Desc().topology);
    const auto type = width == 2 ? 0x1403U : 0x1405U;
    const auto pointer = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset));
    if (firstInstance) OpenGlNative::Require(api.DrawElementsInstancedBaseVertexBaseInstance, "glDrawElementsInstancedBaseVertexBaseInstance")(topology, count, type, pointer, instances, base, firstInstance);
    else if (base) OpenGlNative::Require(api.DrawElementsInstancedBaseVertex, "glDrawElementsInstancedBaseVertex")(topology, count, type, pointer, instances, base);
    else OpenGlNative::Require(api.DrawElementsInstanced, "glDrawElementsInstanced")(topology, count, type, pointer, instances);
    api.BindVertexArray(0);
}

void OpenGlCommandList::DrawSceneGeometry(std::span<const VertexBufferLayoutDesc> buffers,
    std::span<const VertexAttributeDesc> attributes, PrimitiveTopology topology,
    std::uint32_t count, std::uint32_t first)
{
    RequireRecording();
    _sceneGeometry = true; _sceneBuffers = buffers; _sceneAttributes = attributes; _sceneTopology = topology;
    try { DrawIndexed(count, 1, first, 0, 0); }
    catch (...) { _sceneGeometry = false; _sceneBuffers = {}; _sceneAttributes = {}; throw; }
    _sceneGeometry = false; _sceneBuffers = {}; _sceneAttributes = {};
}

// Groups and bindings are logical. GL flattens each resource category densely
// across groups, then by ascending binding number; shader emission uses this
// same mapping. Texture and sampler categories occupy matching texture units.
std::uint32_t BindingSlot(const PipelineLayout& layout, std::uint32_t group, const BindingLayoutEntry& entry)
{
    std::uint64_t slot = 0;
    for (std::uint32_t i = 0; i <= group; ++i)
        for (const auto& declaration : layout.groups[i].entries)
            if (declaration.type == entry.type && (i < group || declaration.binding < entry.binding)) slot += declaration.count;
    if (slot > UINT32_MAX) throw std::out_of_range("OpenGL RHI: physical binding overflow.");
    return static_cast<std::uint32_t>(slot);
}
void OpenGlCommandList::SetBindingSet(std::uint32_t group, const BindingSet& resource)
{
    RequireRecording();
    const auto* set = dynamic_cast<const OpenGlBindingSet*>(&resource);
    if (!set || set->Device() != _device || !_applied) throw std::invalid_argument("OpenGL RHI: binding needs a compatible set and pipeline.");
    const auto& layout = _applied->Desc().pipelineLayout;
    if (group >= layout.groups.size() || set->Desc().layout->Desc() != layout.groups[group])
        throw std::invalid_argument("OpenGL RHI: binding group is outside or incompatible with the pipeline layout.");
    set->ValidateResources();
    Touch();
    auto snapshot = std::make_unique<OpenGlBindingSet>(*_device, set->Desc());
    ApplyBindingSet(group, *snapshot);
    for (const auto& declaration : layout.groups[group].entries)
        if (declaration.type == BindingType::SampledTexture || declaration.type == BindingType::Sampler)
            for (std::uint32_t element = 0; element < declaration.count; ++element)
            {
                const auto slot = BindingSlot(layout, group, declaration) + element;
                if (slot < _units.size()) _units[slot] = {};
            }
    _genericSets[group] = std::move(snapshot);
}
void OpenGlCommandList::ApplyBindingSet(std::uint32_t group, const OpenGlBindingSet& resource)
{
    const auto* set = &resource;
    const auto& layout = _applied->Desc().pipelineLayout;
    auto& api = _device->Api();
    for (const auto& entry : set->Desc().entries)
    {
        const auto& declarations = layout.groups[group].entries;
        const auto declaration = std::find_if(declarations.begin(), declarations.end(), [&](const auto& item) { return item.binding == entry.binding; });
        const auto slot = BindingSlot(layout, group, *declaration) + entry.arrayElement;
        if (declaration->type == BindingType::UniformBuffer || declaration->type == BindingType::StorageBuffer)
        {
            const auto& value = std::get<BufferBinding>(entry.resource);
            const auto& buffer = CheckedBuffer(*_device, *value.buffer);
            const auto limit = GL::GetInteger(declaration->type == BindingType::UniformBuffer ? 0x8A2F : 0x90DD);
            if (slot >= static_cast<unsigned>(limit)) throw std::out_of_range("OpenGL RHI: buffer binding exceeds device limit.");
            OpenGlNative::Require(api.BindBufferRange, "glBindBufferRange")(declaration->type == BindingType::UniformBuffer ? 0x8A11 : 0x90D2,
                slot, buffer.Name(), static_cast<std::ptrdiff_t>(value.offset), static_cast<std::ptrdiff_t>(value.size ? value.size : buffer.Desc().size - value.offset));
        }
        else if (declaration->type == BindingType::Sampler)
        {
            const auto& sampler = dynamic_cast<const OpenGlSampler&>(*std::get<SamplerBinding>(entry.resource).sampler);
            if (slot >= static_cast<unsigned>(GL::GetInteger(0x8B4D))) throw std::out_of_range("OpenGL RHI: sampler binding exceeds device limit.");
            OpenGlNative::Require(api.BindSampler, "glBindSampler")(slot, sampler.Name());
        }
        else
        {
            const auto& view = *std::get<TextureBinding>(entry.resource).view;
            const auto& texture = ViewTexture(view, _device);
            if (declaration->type == BindingType::StorageTexture)
            {
                if (slot >= static_cast<unsigned>(GL::GetInteger(0x8F38))) throw std::out_of_range("OpenGL RHI: image binding exceeds device limit.");
                OpenGlNative::Require(api.BindImageTexture, "glBindImageTexture")(slot, texture.Name(), view.Desc().baseMipLevel, 0, 0, 0x88BA, static_cast<unsigned>(ToGl(texture.Desc().format).Internal));
            }
            else
            {
                if (slot >= static_cast<unsigned>(GL::GetInteger(0x8B4D))) throw std::out_of_range("OpenGL RHI: texture binding exceeds device limit.");
                GL::ActiveTexture(static_cast<GL::TextureUnit>(0x84C0 + slot));
                GL::BindTexture(GL::TextureTarget::Texture2D, texture.Name());
                GL::ActiveTexture(GL::TextureUnit::Texture0);
            }
        }
    }
}
void OpenGlCommandList::CopyBuffer(const Buffer& source, std::uint64_t sourceOffset, Buffer& destination, std::uint64_t destinationOffset, std::uint64_t size)
{
    RequireRecording();
    const auto& from = CheckedBuffer(*_device, source); const auto& to = CheckedBuffer(*_device, destination);
    if (!HasAny(from.State(), ResourceState::CopySrc) || to.State() != ResourceState::CopyDst)
        throw std::invalid_argument("OpenGL RHI: buffer copies require CopySrc and CopyDst states.");
    CheckBufferRange(source, sourceOffset, size); CheckBufferRange(destination, destinationOffset, size);
    if (!Has(source.Desc().usage, BufferUsage::TransferSrc) || !Has(destination.Desc().usage, BufferUsage::TransferDst)
        || (&source == &destination && sourceOffset < destinationOffset + size && destinationOffset < sourceOffset + size))
        throw std::invalid_argument("OpenGL RHI: invalid or overlapping buffer copy.");
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F36), from.Name()); GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F37), to.Name());
    OpenGlNative::Require(_device->Api().CopyBufferSubData, "glCopyBufferSubData")(0x8F36, 0x8F37, sourceOffset, destinationOffset, size);
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F36), 0); GL::BindBuffer(static_cast<GL::BufferTarget>(0x8F37), 0);
}
unsigned TexelBytes(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8Unorm: return 1;
    case TextureFormat::RG8Unorm: case TextureFormat::R16Float: case TextureFormat::D16Unorm: return 2;
    case TextureFormat::RGB8Unorm: return 3;
    case TextureFormat::RG16Float: case TextureFormat::R32Float: case TextureFormat::D32Float:
    case TextureFormat::D24UnormS8Uint: case TextureFormat::RGBA8Unorm: case TextureFormat::RGBA8Srgb:
    case TextureFormat::BGRA8Unorm: case TextureFormat::BGRA8Srgb: return 4;
    case TextureFormat::RG32Float: case TextureFormat::RGBA16Float: case TextureFormat::D32FloatS8Uint: return 8;
    case TextureFormat::RGB32Float: return 12;
    case TextureFormat::RGBA32Float: return 16;
    default: throw std::invalid_argument("OpenGL RHI: undefined copy format.");
    }
}
unsigned CheckTextureCopy(const Texture& texture, const Buffer& buffer, const BufferTextureCopy& region)
{
    const auto& desc = texture.Desc();
    if (desc.depth != 1 || desc.arrayLayers != 1 || region.mipLevel || region.arrayLayer || region.z
        || region.depth != 1 || !region.width || !region.height || region.x > desc.width || region.width > desc.width - region.x
        || region.y > desc.height || region.height > desc.height - region.y)
        throw std::out_of_range("OpenGL RHI: unsupported subresource or texture copy bounds.");
    const bool depth = IsDepthFormat(desc.format);
    const bool packed = desc.format == TextureFormat::D24UnormS8Uint || desc.format == TextureFormat::D32FloatS8Uint;
    // Packed depth/stencil needs two independently specified transfers to
    // match Vulkan's aspect byte layout. Never treat a stencil byte as a
    // packed pixel: that would overwrite the caller's buffer range.
    if (packed || region.aspect == TextureAspect::Stencil
        || (region.aspect == TextureAspect::Depth && !depth)
        || (region.aspect == TextureAspect::Color && depth))
        throw std::invalid_argument("OpenGL RHI: unsupported texture copy aspect.");
    const unsigned bytes = TexelBytes(desc.format);
    const std::uint64_t row = region.bytesPerRow ? region.bytesPerRow : static_cast<std::uint64_t>(region.width) * bytes;
    if (row < static_cast<std::uint64_t>(region.width) * bytes || row % bytes || row / bytes > INT32_MAX
        || (region.rowsPerImage && region.rowsPerImage < region.height)) throw std::invalid_argument("OpenGL RHI: invalid texture copy row pitch.");
    CheckBufferRange(buffer, region.bufferOffset, row * (region.height - 1) + static_cast<std::uint64_t>(region.width) * bytes);
    return bytes;
}
void OpenGlCommandList::CopyBufferToTexture(const Buffer& source, Texture& destination, const BufferTextureCopy& region)
{
    RequireRecording();
    const auto& buffer = CheckedBuffer(*_device, source); auto& texture = Native(destination, _device);
    if (!HasAny(buffer.State(), ResourceState::CopySrc) || texture.State() != ResourceState::CopyDst)
        throw std::invalid_argument("OpenGL RHI: image uploads require CopySrc and CopyDst states.");
    const auto bytes = CheckTextureCopy(destination, source, region);
    if (texture.IsRenderbuffer() || !Has(source.Desc().usage, BufferUsage::TransferSrc) || !Has(destination.Desc().usage, TextureUsage::TransferDst))
        throw std::invalid_argument("OpenGL RHI: invalid buffer to texture copy usage.");
    if (!texture.HasStorage()) _device->AllocateStorage(texture, texture.Desc().width, texture.Desc().height);
    const auto format = ToGl(texture.Desc().format);
    const int alignment = GL::GetInteger(0x0CF5), rowLength = GL::GetInteger(0x0CF2);
    const int previousTexture = GL::GetInteger(0x8069), previousUnpack = GL::GetInteger(0x88EF);
    GL::PixelStore(static_cast<GL::PixelStoreParameter>(0x0CF5), 1);
    GL::PixelStore(static_cast<GL::PixelStoreParameter>(0x0CF2), region.bytesPerRow ? region.bytesPerRow / bytes : 0);
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x88EC), buffer.Name());
    GL::BindTexture(GL::TextureTarget::Texture2D, texture.Name());
    GL::TexSubImage2D(GL::TextureTarget::Texture2D, 0, region.x, region.y, region.width, region.height,
        format.Format, format.Type, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(region.bufferOffset)));
    GL::BindTexture(GL::TextureTarget::Texture2D, previousTexture);
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x88EC), previousUnpack);
    GL::PixelStore(static_cast<GL::PixelStoreParameter>(0x0CF5), alignment); GL::PixelStore(static_cast<GL::PixelStoreParameter>(0x0CF2), rowLength);
}
void OpenGlCommandList::CopyTextureToBuffer(const Texture& source, Buffer& destination, const BufferTextureCopy& region)
{
    RequireRecording();
    const auto& buffer = CheckedBuffer(*_device, destination); const auto& texture = Native(source, _device);
    if (!HasAny(texture.State(), ResourceState::CopySrc) || buffer.State() != ResourceState::CopyDst)
        throw std::invalid_argument("OpenGL RHI: image readback requires CopySrc and CopyDst states.");
    const auto bytes = CheckTextureCopy(source, destination, region);
    if (texture.IsRenderbuffer() || !Has(source.Desc().usage, TextureUsage::TransferSrc) || !Has(destination.Desc().usage, BufferUsage::TransferDst))
        throw std::invalid_argument("OpenGL RHI: invalid texture to buffer copy usage.");
    const auto format = ToGl(texture.Desc().format);
    const int alignment = GL::GetInteger(0x0D05), rowLength = GL::GetInteger(0x0D02), previous = GL::GetInteger(0x8CAA);
    const int previousPack = GL::GetInteger(0x88ED);
    const auto framebuffer = GL::GenFramebuffer();
    GL::BindFramebuffer(GL::FramebufferTarget::ReadFramebuffer, framebuffer);
    const bool depth = IsDepthFormat(texture.Desc().format);
    const bool stencil = texture.Desc().format == TextureFormat::D24UnormS8Uint || texture.Desc().format == TextureFormat::D32FloatS8Uint;
    GL::FramebufferTexture2D(GL::FramebufferTarget::ReadFramebuffer, static_cast<GL::FramebufferAttachment>(depth ? (stencil ? 0x821A : 0x8D00) : 0x8CE0), GL::TextureTarget::Texture2D, texture.Name(), 0);
    GL::ReadBuffer(static_cast<GL::ReadBufferMode>(depth ? 0 : 0x8CE0));
    GL::PixelStore(static_cast<GL::PixelStoreParameter>(0x0D05), 1); GL::PixelStore(static_cast<GL::PixelStoreParameter>(0x0D02), region.bytesPerRow ? region.bytesPerRow / bytes : 0);
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x88EB), buffer.Name());
    GL::ReadPixels(region.x, region.y, region.width, region.height, format.Format, format.Type, reinterpret_cast<void*>(static_cast<std::uintptr_t>(region.bufferOffset)));
    GL::BindBuffer(static_cast<GL::BufferTarget>(0x88EB), previousPack);
    GL::PixelStore(static_cast<GL::PixelStoreParameter>(0x0D05), alignment); GL::PixelStore(static_cast<GL::PixelStoreParameter>(0x0D02), rowLength);
    GL::BindFramebuffer(GL::FramebufferTarget::ReadFramebuffer, previous); GL::DeleteFramebuffer(framebuffer);
}
void OpenGlCommandList::Transition(Buffer& resource, ResourceState before, ResourceState after)
{
    RequireRecording();
    auto& buffer = CheckedBuffer(*_device, resource);
    if (buffer.State() != before || !IsValidTransition(before, after)
        || !IsValidBufferState(buffer.Desc(), before) || !IsValidBufferState(buffer.Desc(), after))
        throw std::invalid_argument("OpenGL RHI: buffer transition does not match its tracked state or declared usage.");
    if (HasAny(before, ResourceState::ShaderWrite)) OpenGlNative::Require(_device->Api().MemoryBarrier, "glMemoryBarrier")(0xFFFFFFFFU);
    buffer.State(after);
}
void OpenGlCommandList::Transition(Texture& resource, ResourceState before, ResourceState after)
{
    RequireRecording();
    auto& texture = Native(resource, _device);
    if (texture.State() != before || !IsValidTransition(before, after)
        || !IsValidTextureState(texture.Desc(), before) || !IsValidTextureState(texture.Desc(), after))
        throw std::invalid_argument("OpenGL RHI: image transition does not match its tracked state, usage or format.");
    if (HasAny(before, ResourceState::ShaderWrite)) OpenGlNative::Require(_device->Api().MemoryBarrier, "glMemoryBarrier")(0xFFFFFFFFU);
    texture.State(after);
}
