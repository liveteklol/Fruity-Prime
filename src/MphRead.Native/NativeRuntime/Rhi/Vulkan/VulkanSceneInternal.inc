// Included inside the Vulkan backend after VulkanCommandList: the scene's
// programs, its constant sink, and its mesh and transient geometry.

using ::OpenTK::Mathematics::Matrix4;
using ::OpenTK::Mathematics::Vector3;
using ::OpenTK::Mathematics::Vector4;

// Each call writes the program current on the device, as glUniform writes the
// program in use; a name the program does not declare is ignored, as a
// location of -1 is.
class VulkanSceneConstants final : public ShaderConstantSink
{
public:
    explicit VulkanSceneConstants(std::shared_ptr<VulkanDeviceState> device) : _device(std::move(device)) {}
    void SelectMaterialOwner(std::uint64_t identity) override
    { if (auto* p = Current()) p->SelectMaterialOwner(identity); }

    void Set(const FrameConstants& c) override { SetView(c.View); SetProjection(c.Projection); }
    void Set(const SceneLightConstants& c) override
    {
        for (std::size_t i = 0; i < SceneLightCount; ++i) SetLight(i, c.Lights[i]);
    }
    void SetLight(std::size_t index, const LightConstants& c) override
    {
        if (index == 0) { Vec3(SceneShaderAbi::ConstantIndex("light1vec"), c.Vector); Vec3(SceneShaderAbi::ConstantIndex("light1col"), c.Color); }
        else if (index == 1) { Vec3(SceneShaderAbi::ConstantIndex("light2vec"), c.Vector); Vec3(SceneShaderAbi::ConstantIndex("light2col"), c.Color); }
    }
    void Set(const SceneFogConstants& c) override
    {
        Vec4(SceneShaderAbi::ConstantIndex("fog_color"), c.Color); Float(SceneShaderAbi::ConstantIndex("fog_min"), c.MinDistance); Float(SceneShaderAbi::ConstantIndex("fog_max"), c.MaxDistance);
    }
    void Set(const MaterialConstants& c) override { SetSurface(c); Float(SceneShaderAbi::ConstantIndex("mat_alpha"), c.Alpha); }
    void Set(const DrawConstants& c) override
    {
        const std::size_t count = std::min(c.MatrixStack.size() / 16U, MatrixStackCapacity);
        if (count == 0) return;
        if (auto* p = Current()) p->WriteArray(SceneShaderAbi::ConstantIndex("mtx_stack"), c.MatrixStack.data(), 16, count);
    }
    void Set(const CelPostConstants& c) override
    {
        Float(SceneShaderAbi::ConstantIndex("texel_w"), c.TexelWidth); Float(SceneShaderAbi::ConstantIndex("texel_h"), c.TexelHeight); Float(SceneShaderAbi::ConstantIndex("outline"), c.Outline);
        Float(SceneShaderAbi::ConstantIndex("near_plane"), c.NearPlane); Float(SceneShaderAbi::ConstantIndex("far_plane"), c.FarPlane);
        Float(SceneShaderAbi::ConstantIndex("depth_quantum"), c.DepthQuantum); Int(SceneShaderAbi::ConstantIndex("probe"), c.Probe ? 1 : 0);
    }
    void SetFadeColor(const Vector4& color) override { Vec4(SceneShaderAbi::ConstantIndex("fade_color"), color); }
    void SetLayerAlpha(float alpha) override { Float(SceneShaderAbi::ConstantIndex("alpha"), alpha); }
    void SetUseMask(bool useMask) override { Int(SceneShaderAbi::ConstantIndex("use_mask"), useMask ? 1 : 0); }
    void SetViewSize(float width, float height) override { Float(SceneShaderAbi::ConstantIndex("view_width"), width); Float(SceneShaderAbi::ConstantIndex("view_height"), height); }
    void Set(const DisruptionPostConstants& c) override
    {
        Float(SceneShaderAbi::ConstantIndex("shift_fac"), c.ShiftFactor); Int(SceneShaderAbi::ConstantIndex("shift_idx"), c.ShiftIndex);
        Float(SceneShaderAbi::ConstantIndex("lerp_fac"), c.LerpFactor); Float(SceneShaderAbi::ConstantIndex("white_fac"), c.WhiteoutFactor);
    }
    void SetShiftTable(std::span<const float> table) override
    {
        if (auto* p = Current()) p->WriteArray(SceneShaderAbi::ConstantIndex("shift_table"), table.data(), 1, std::min(table.size(), ShiftTableLength));
    }
    void SetWhiteoutTable(std::span<const float> table) override
    {
        if (auto* p = Current()) p->WriteArray(SceneShaderAbi::ConstantIndex("white_table"), table.data(), 1, std::min(table.size(), WhiteoutTableLength));
    }
    void SetView(const Matrix4& view) override { Mat4(SceneShaderAbi::ConstantIndex("view_mtx"), view); }
    void SetProjection(const Matrix4& projection) override { Mat4(SceneShaderAbi::ConstantIndex("proj_mtx"), projection); }
    void SetFogEnabled(bool enabled) override { Int(SceneShaderAbi::ConstantIndex("fog_enable"), enabled ? 1 : 0); }
    void SetCelBands(std::int32_t bands) override { Int(SceneShaderAbi::ConstantIndex("cel_bands"), bands); }
    void SetShowColors(bool show) override { Int(SceneShaderAbi::ConstantIndex("show_colors"), show ? 1 : 0); }
    void SetBillboard(const Matrix4& viewInverse) override { Mat4(SceneShaderAbi::ConstantIndex("view_inv_mtx"), viewInverse); }
    void SetSurface(const MaterialConstants& c) override
    {
        Int(SceneShaderAbi::ConstantIndex("use_light"), c.UseLight ? 1 : 0);
        Vec3(SceneShaderAbi::ConstantIndex("diffuse"), c.Diffuse); Vec3(SceneShaderAbi::ConstantIndex("ambient"), c.Ambient); Vec3(SceneShaderAbi::ConstantIndex("specular"), c.Specular);
        Vec3(SceneShaderAbi::ConstantIndex("emission"), c.Emission); Int(SceneShaderAbi::ConstantIndex("mat_mode"), c.PolygonMode);
    }
    void SetMaterialAlpha(float alpha) override { Float(SceneShaderAbi::ConstantIndex("mat_alpha"), alpha); }
    void SetUseTexture(bool enabled) override { Int(SceneShaderAbi::ConstantIndex("use_texture"), enabled ? 1 : 0); }
    void SetTexgen(std::int32_t mode, const Matrix4& textureMatrix) override
    {
        Int(SceneShaderAbi::ConstantIndex("texgen_mode"), mode); Mat4(SceneShaderAbi::ConstantIndex("tex_mtx"), textureMatrix);
    }
    void SetOverride(const Vector4* color) override
    {
        Int(SceneShaderAbi::ConstantIndex("use_override"), color ? 1 : 0);
        if (color) Vec4(SceneShaderAbi::ConstantIndex("override_color"), *color);
    }
    void SetOverrideColor(const Vector4& color) override { Vec4(SceneShaderAbi::ConstantIndex("override_color"), color); }
    void SetPaletteOverride(const Vector4* color) override
    {
        Int(SceneShaderAbi::ConstantIndex("use_pal_override"), color ? 1 : 0);
        if (color) Vec4(SceneShaderAbi::ConstantIndex("pal_override_color"), *color);
    }
    void SetFlatColor(const Vector3* color) override
    {
        Int(SceneShaderAbi::ConstantIndex("use_flat"), color ? 1 : 0);
        if (color) Vec3(SceneShaderAbi::ConstantIndex("flat_color"), *color);
    }
    // glColor4f / glTexCoord3f: context state, not program state.
    void SetInheritedColor(const Vector4& color) override
    {
        _device->CurrentColor = {color.X, color.Y, color.Z, color.W};
    }
    void SetInheritedTexCoord(const Vector3& texCoord) override
    {
        _device->CurrentTexCoord = {texCoord.X, texCoord.Y, texCoord.Z};
    }

private:
    [[nodiscard]] VulkanSceneProgram* Current() const
    {
        return static_cast<VulkanSceneProgram*>(_device->CurrentSceneProgram);
    }
    void Float(std::size_t slot, float value) { if (auto* p = Current()) p->Write(slot, &value, 4, SceneShaderAbi::ValueType::Float); }
    void Int(std::size_t slot, std::int32_t value) { if (auto* p = Current()) p->Write(slot, &value, 4, SceneShaderAbi::ValueType::Int); }
    void Vec3(std::size_t slot, const Vector3& v)
    {
        const float data[3]{v.X, v.Y, v.Z};
        if (auto* p = Current()) p->Write(slot, data, sizeof(data), SceneShaderAbi::ValueType::Vec3);
    }
    void Vec4(std::size_t slot, const Vector4& v)
    {
        const float data[4]{v.X, v.Y, v.Z, v.W};
        if (auto* p = Current()) p->Write(slot, data, sizeof(data), SceneShaderAbi::ValueType::Vec4);
    }
    // Untransposed: Matrix4's memory order is what glUniformMatrix4fv(false) reads.
    void Mat4(std::size_t slot, const Matrix4& m)
    {
        if (auto* p = Current()) p->Write(slot, &m.M11, 64, SceneShaderAbi::ValueType::Mat4);
    }

    std::shared_ptr<VulkanDeviceState> _device;
};

class VulkanSceneShaderSet final : public SceneShaderSet
{
public:
    VulkanSceneShaderSet(std::shared_ptr<VulkanDeviceState> device,
        std::span<const float> toonTable, std::span<const float> shiftTable)
        : _device(std::move(device)), _constants(_device)
    {
        Add(SceneProgram::Main, Generated::main_vert, Generated::main_frag, Generated::main_uniforms,
            Generated::main_textures, Generated::main_blocks);
        Add(SceneProgram::Composite, Generated::composite_vert, Generated::composite_frag,
            Generated::composite_uniforms, Generated::composite_textures, Generated::composite_blocks);
        Add(SceneProgram::Shift, Generated::shift_vert, Generated::shift_frag, Generated::shift_uniforms,
            Generated::shift_textures, Generated::shift_blocks);
        Add(SceneProgram::CelOutline, Generated::cel_vert, Generated::cel_frag, Generated::cel_uniforms,
            Generated::cel_textures, Generated::cel_blocks);
        Add(SceneProgram::Backdrop, Generated::backdrop_vert, Generated::backdrop_frag,
            Generated::backdrop_uniforms, Generated::backdrop_textures, Generated::backdrop_blocks);
        // What OpenGlSceneShaderSet sets as it links: the shift table on the
        // shift program and the toon table on the main one, which is left current.
        Program(SceneProgram::Shift).WriteArray(SceneShaderAbi::ConstantIndex("shift_table"), shiftTable.data(), 1,
            std::min(shiftTable.size(), ShiftTableLength));
        Program(SceneProgram::Main).WriteArray(SceneShaderAbi::ConstantIndex("toon_table"), toonTable.data(), 3, toonTable.size() / 3U);
        _device->CurrentSceneProgram = &Program(SceneProgram::Main);
    }

    ~VulkanSceneShaderSet() override
    {
        try { _device->FlushScene(); } catch (...) {}
        for (auto& program : _programs)
        {
            if (_device->CurrentSceneProgram == program.get()) _device->CurrentSceneProgram = nullptr;
            // Each program retires its own fixed/persistent descriptors.
        }
    }

    [[nodiscard]] const Shader& Vertex(SceneProgram program) const override { return *Program(program).Vertex; }
    [[nodiscard]] const Shader& Fragment(SceneProgram program) const override { return *Program(program).Fragment; }
    [[nodiscard]] ShaderConstantSink& Constants() override { return _constants; }

private:
    [[nodiscard]] VulkanSceneProgram& Program(SceneProgram id) const
    {
        for (const auto& program : _programs)
            if (program->Id == id) return *program;
        throw std::invalid_argument("Vulkan RHI: unknown scene program.");
    }

    template <typename VertexWords, typename FragmentWords, typename Uniforms, typename Textures, typename Blocks>
    void Add(SceneProgram id, const VertexWords& vertexWords, const FragmentWords& fragmentWords,
        const Uniforms& uniforms, const Textures& textures, const Blocks& blocks)
    {
        auto program = std::make_unique<VulkanSceneProgram>(blocks, uniforms);
        program->Id = id;
        program->Device = _device;
        const auto shader = [&](const auto& words, ShaderStage stage) {
            ShaderDesc desc{};
            desc.stage = stage;
            desc.format = ShaderCodeFormat::SpirV;
            desc.code.resize(words.size() * sizeof(std::uint32_t));
            std::memcpy(desc.code.data(), words.data(), desc.code.size());
            auto module = std::make_unique<VulkanShader>(_device, desc);
            module->SceneProgram = program.get();
            return module;
        };
        program->Vertex = shader(vertexWords, ShaderStage::Vertex);
        program->Fragment = shader(fragmentWords, ShaderStage::Fragment);
        std::array<BindingLayoutDesc, SceneShaderAbi::GroupCount> layouts{};
        for (const auto& block : blocks)
        {
            std::uint8_t slot = 0;
            if (!block.small && DynamicUniformGroup(block.group))
                for (const auto& other : blocks)
                    if (!other.small && other.group == block.group && other.binding < block.binding) ++slot;
            if (slot >= 4) throw std::logic_error("Vulkan scene program: too many dynamic uniform blocks in a group.");
            program->DynamicSlot.push_back(slot);
        }
        for (const auto& block : blocks)
        {
            if (block.small) continue;
            // The frame and draw groups' blocks are bound at offsets in the
            // upload ring, so a new slice needs no new descriptor set.
            const bool dynamic = DynamicUniformGroup(block.group);
            layouts.at(block.group).entries.push_back({block.binding, BindingType::UniformBuffer, ShaderStage::AllGraphics, 1, dynamic});
        }
        for (const auto& texture : textures)
        {
            auto& layout = layouts.at(texture.group);
            layout.entries.push_back({texture.image, BindingType::SampledTexture, ShaderStage::AllGraphics, 1});
            layout.entries.push_back({texture.sampler, BindingType::Sampler, ShaderStage::AllGraphics, 1});
            program->Textures.push_back(texture);
        }
        for (std::uint32_t group = 0; group < SceneShaderAbi::GroupCount; ++group)
            program->Layouts[group] = std::make_unique<VulkanBindingLayout>(_device, layouts[group]);
        if (id == SceneProgram::Main) AddFast(*program, uniforms, textures, blocks);
        _programs.push_back(std::move(program));
    }

    // main_fast's modules and layouts, and where its records are in main's
    // blocks. Left out (FastVertex null) when main's blocks are not the ones
    // main_fast was generated for, so the ordinary path draws.
    template <typename Uniforms, typename Textures, typename Blocks>
    void AddFast(VulkanSceneProgram& program, const Uniforms& uniforms, const Textures& textures, const Blocks& blocks)
    {
        constexpr std::array<std::string_view, 5> records{"Frame", "Light", "Fog", "Material", "Draw"};
        std::array<bool, 5> found{};
        bool small = false;
        for (std::size_t i = 0; i < blocks.size(); ++i)
        {
            if (blocks[i].small) { program.FastSmall = i; small = true; continue; }
            for (std::size_t k = 0; k < records.size(); ++k)
                if (blocks[i].semantic == records[k]) { program.FastBlocks[k] = i; found[k] = true; }
        }
        const auto matrices = std::find_if(uniforms.begin(), uniforms.end(),
            [](const auto& member) { return member.name == "mtx_stack"; });
        if (!small || std::find(found.begin(), found.end(), false) != found.end() || matrices == uniforms.end()
            || matrices->block != program.FastBlocks[4] || matrices->offset % 16 || textures.size() != 1
            || textures[0].group != 1)
            return;
        program.FastMatrixOffset = matrices->offset;
        program.FastDrawBytes = matrices->offset;
        program.FastPushBytes = blocks[program.FastSmall].size;
        const auto shader = [&](const auto& words, ShaderStage stage) {
            ShaderDesc desc{};
            desc.stage = stage;
            desc.format = ShaderCodeFormat::SpirV;
            desc.code.resize(words.size() * sizeof(std::uint32_t));
            std::memcpy(desc.code.data(), words.data(), desc.code.size());
            return std::make_unique<VulkanShader>(_device, desc);
        };
        BindingLayoutDesc recordLayout{};
        for (std::uint32_t binding = 0; binding < records.size(); ++binding)
            recordLayout.entries.push_back({binding, BindingType::UniformBuffer, ShaderStage::AllGraphics, 1, true});
        BindingLayoutDesc textureLayout{};
        textureLayout.entries.push_back({textures[0].image, BindingType::SampledTexture, ShaderStage::AllGraphics, 1});
        textureLayout.entries.push_back({textures[0].sampler, BindingType::Sampler, ShaderStage::AllGraphics, 1});
        program.FastLayouts[0] = std::make_unique<VulkanBindingLayout>(_device, recordLayout);
        program.FastLayouts[1] = std::make_unique<VulkanBindingLayout>(_device, textureLayout);
        program.FastVertex = shader(Generated::main_fast_vert, ShaderStage::Vertex);
        program.FastFragment = shader(Generated::main_fast_frag, ShaderStage::Fragment);
    }

    std::shared_ptr<VulkanDeviceState> _device;
    std::vector<std::unique_ptr<VulkanSceneProgram>> _programs;
    VulkanSceneConstants _constants;
};

class VulkanGpuMesh final : public MphRead::GpuMeshResource
{
public:
    VulkanGpuMesh(VulkanGraphicsDevice& device, VulkanCommandList& commands, const MphRead::RendererGeometry& geometry)
        : _device(device.State()), _commands(commands), _terminal(geometry.TerminalState),
          _terminalState(geometry.TerminalAttributeState), _states(geometry.VertexAttributeStates)
    {
        using MphRead::SceneVertexAttributeState;
        if (_states.size() != geometry.Vertices.size())
            throw std::invalid_argument("GPU mesh vertex attribute-state count does not match the vertex count.");
        bool anyColor = false, allColor = true, anyNormal = false, allNormal = true;
        _vertices.reserve(geometry.Vertices.size() * 13U);
        for (std::size_t i = 0; i < geometry.Vertices.size(); ++i)
        {
            const auto& v = geometry.Vertices[i];
            _vertices.insert(_vertices.end(), {v.Position.X, v.Position.Y, v.Position.Z,
                v.Normal.X, v.Normal.Y, v.Normal.Z, v.Color.X, v.Color.Y, v.Color.Z, v.Color.W,
                v.TexCoord.X, v.TexCoord.Y, static_cast<float>(v.MatrixIndex)});
            _matrices = std::max(_matrices, std::min<std::uint32_t>(v.MatrixIndex, 31U) + 1U);
            const bool color = MphRead::HasSceneVertexAttributeState(_states[i], SceneVertexAttributeState::Color);
            const bool normal = MphRead::HasSceneVertexAttributeState(_states[i], SceneVertexAttributeState::Normal);
            anyColor |= color; allColor &= color; anyNormal |= normal; allNormal &= normal;
        }
        _colorMode = !anyColor ? Mode::Inherited : allColor ? Mode::Static : Mode::Mixed;
        _normalMode = !anyNormal ? Mode::Inherited : allNormal ? Mode::Static : Mode::Mixed;
        const auto indices = MphRead::BuildGpuMeshTriangleIndices(geometry);
        _indexCount = static_cast<std::uint32_t>(indices.size());
        if (!_vertices.empty())
            _vertexBuffer = Upload(device, std::as_bytes(std::span(_vertices)), BufferUsage::Vertex);
        if (!indices.empty())
            _indexBuffer = Upload(device, std::as_bytes(std::span(indices)), BufferUsage::Index);
    }

    void Draw() override
    {
        using MphRead::SceneVertexAttributeState;
        if (_vertexBuffer && _indexBuffer && _indexCount)
        {
            constexpr VkDeviceSize stride = 13U * sizeof(float);
            const VkBuffer vb = _vertexBuffer->Native();
            VulkanCommandList::SceneDraw draw{};
            draw.Streams[0] = {vb, 0, stride};
            draw.Streams[3] = {vb, 10U * sizeof(float), stride};
            draw.Streams[2] = Stream(_colorMode, 6, 4, SceneVertexAttributeState::Color,
                _device->CurrentColor.data());
            draw.Streams[1] = Stream(_normalMode, 3, 3, SceneVertexAttributeState::Normal,
                _device->CurrentNormal.data());
            draw.IndexBuffer = _indexBuffer->Native();
            draw.IndexCount = _indexCount;
            draw.Matrices = _matrices;
            _commands.DrawScene(draw);
        }
        // The current attributes the display list leaves behind.
        if (MphRead::HasSceneVertexAttributeState(_terminalState, SceneVertexAttributeState::Color))
            _device->CurrentColor = {_terminal.Color.X, _terminal.Color.Y, _terminal.Color.Z, _terminal.Color.W};
        if (MphRead::HasSceneVertexAttributeState(_terminalState, SceneVertexAttributeState::Normal))
            _device->CurrentNormal = {_terminal.Normal.X, _terminal.Normal.Y, _terminal.Normal.Z};
        _device->CurrentTexCoord = {_terminal.TexCoord.X, _terminal.TexCoord.Y,
            static_cast<float>(_terminal.MatrixIndex)};
    }

private:
    enum class Mode : std::uint8_t { Inherited, Static, Mixed };
    // Matrix-stack entries the vertices index (performance mode copies these).
    std::uint32_t _matrices = 1;

    static std::unique_ptr<VulkanBuffer> Upload(VulkanGraphicsDevice& device, std::span<const std::byte> data,
        BufferUsage usage)
    {
        BufferDesc desc{};
        desc.size = data.size();
        desc.usage = usage | BufferUsage::TransferDst;
        desc.memoryUsage = MemoryUsage::GpuOnly;
        desc.initialState = usage == BufferUsage::Index ? ResourceState::IndexBuffer : ResourceState::VertexBuffer;
        auto buffer = std::make_unique<VulkanBuffer>(device.State(), desc);
        device.WriteBuffer(*buffer, 0, data);
        return buffer;
    }

    VulkanCommandList::SceneStream Stream(Mode mode, std::size_t component, std::size_t count,
        MphRead::SceneVertexAttributeState flag, const float* current)
    {
        constexpr VkDeviceSize stride = 13U * sizeof(float);
        if (mode == Mode::Static) return {_vertexBuffer->Native(), component * sizeof(float), stride};
        if (mode == Mode::Inherited)
        {
            auto slice = _commands.Allocate(16);
            std::memcpy(slice.Data, current, count * sizeof(float));
            return {slice.Buffer, slice.Offset, 0};
        }
        const std::size_t vertices = _states.size();
        auto slice = _commands.Allocate(vertices * count * sizeof(float));
        auto* out = reinterpret_cast<float*>(slice.Data);
        for (std::size_t i = 0; i < vertices; ++i)
        {
            const float* source = MphRead::HasSceneVertexAttributeState(_states[i], flag)
                ? &_vertices[i * 13U + component] : current;
            std::memcpy(out + i * count, source, count * sizeof(float));
        }
        return {slice.Buffer, slice.Offset, count * sizeof(float)};
    }

    std::shared_ptr<VulkanDeviceState> _device;
    VulkanCommandList& _commands;
    MphRead::SceneVertex _terminal{};
    MphRead::SceneVertexAttributeState _terminalState{};
    std::vector<MphRead::SceneVertexAttributeState> _states;
    std::vector<float> _vertices;
    std::uint32_t _indexCount = 0;
    Mode _colorMode = Mode::Inherited;
    Mode _normalMode = Mode::Inherited;
    std::unique_ptr<VulkanBuffer> _vertexBuffer;
    std::unique_ptr<VulkanBuffer> _indexBuffer;
};

struct WindowUi::Impl final
{
    explicit Impl(VulkanGraphicsDevice& device)
        : Device(device.State()), Shaders(device.State(), {}, {}),
          Commands(std::make_unique<VulkanCommandList>(device.State()))
    {
        Commands->Begin();
        const auto pipeline = [&](SceneProgram program, bool blend) {
            GraphicsPipelineDesc desc{};
            desc.vertexShader = &Shaders.Vertex(program);
            desc.fragmentShader = &Shaders.Fragment(program);
            desc.rasterizer.cullMode = CullMode::None;
            BlendAttachmentDesc attachment{};
            attachment.blendEnable = blend;
            attachment.srcColorFactor = BlendFactor::One;
            attachment.dstColorFactor = BlendFactor::OneMinusSrcAlpha;
            attachment.srcAlphaFactor = BlendFactor::One;
            attachment.dstAlphaFactor = BlendFactor::OneMinusSrcAlpha;
            desc.blendAttachments.push_back(attachment);
            desc.colorFormats.push_back(TextureFormat::RGBA8Unorm);
            return std::make_unique<VulkanGraphicsPipeline>(Device, desc, VulkanGraphicsPipeline::Deferred{});
        };
        Premultiplied = pipeline(SceneProgram::Composite, true);
        Opaque = pipeline(SceneProgram::Composite, false);
        Backdrop = pipeline(SceneProgram::Backdrop, false);
        Noise = CreateBackdropNoiseResources(device);
    }

    void Quad(std::span<const WindowQuadVertex, 4> strip)
    {
        auto vertices = Commands->Allocate(4U * 32U);
        auto* out = reinterpret_cast<float*>(vertices.Data);
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float data[8]{strip[i].Position[0], strip[i].Position[1], 0.0F,
                strip[i].TexCoord[0], strip[i].TexCoord[1], 0.0F, strip[i].TexCoord1[0], strip[i].TexCoord1[1]};
            std::memcpy(out + i * 8U, data, sizeof(data));
        }
        // The strip's two triangles, in OpenGL's order.
        const std::uint32_t order[6]{0, 1, 2, 2, 1, 3};
        auto index = Commands->Allocate(sizeof(order), 4);
        std::memcpy(index.Data, order, sizeof(order));
        auto constant = Commands->Allocate(16);
        const float zero[4]{0.0F, 0.0F, 1.0F, 1.0F};
        std::memcpy(constant.Data, zero, sizeof(zero));
        VulkanCommandList::SceneDraw draw{};
        draw.Streams[0] = {vertices.Buffer, vertices.Offset, 32};
        draw.Streams[1] = {constant.Buffer, constant.Offset, 0};
        draw.Streams[2] = {constant.Buffer, constant.Offset, 0};
        draw.Streams[3] = {vertices.Buffer, vertices.Offset + 12, 32};
        draw.Streams[4] = {vertices.Buffer, vertices.Offset + 24, 32};
        draw.IndexBuffer = index.Buffer;
        draw.IndexOffset = index.Offset;
        draw.IndexCount = 6;
        Commands->DrawScene(draw);
    }

    [[nodiscard]] VulkanSceneProgram& Program(SceneProgram id)
    {
        return *static_cast<VulkanSceneProgram*>(
            static_cast<const VulkanShader&>(Shaders.Fragment(id)).SceneProgram);
    }

    std::shared_ptr<VulkanDeviceState> Device;
    VulkanSceneShaderSet Shaders;
    std::unique_ptr<VulkanCommandList> Commands;
    std::unique_ptr<VulkanGraphicsPipeline> Premultiplied;
    std::unique_ptr<VulkanGraphicsPipeline> Opaque;
    std::unique_ptr<VulkanGraphicsPipeline> Backdrop;
    BackdropNoiseResources Noise;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
};

WindowUi::WindowUi(GraphicsDevice& device)
    : _impl(std::make_unique<Impl>(dynamic_cast<VulkanGraphicsDevice&>(device)))
{
}

WindowUi::~WindowUi() = default;

void WindowUi::Begin(std::uint32_t width, std::uint32_t height, bool clear)
{
    auto& commands = *_impl->Commands;
    _impl->Width = std::max(width, 1U);
    _impl->Height = std::max(height, 1U);
    std::array<RenderingColorAttachment, 1> color{};
    color[0].loadOp = clear ? LoadOp::Clear : LoadOp::Load;
    color[0].clearValue = ClearColor{0.0F, 0.0F, 0.0F, 1.0F};
    RenderingDepthStencilAttachment depth{};
    depth.depthLoadOp = clear ? LoadOp::Clear : LoadOp::Load;
    depth.stencilLoadOp = clear ? LoadOp::Clear : LoadOp::Load;
    RenderingInfo info{};
    info.width = _impl->Width;
    info.height = _impl->Height;
    info.swapchain = true;
    info.colorAttachments = color;
    info.depthStencilAttachment = &depth;
    commands.BeginRendering(info);
    commands.SetViewport(Viewport{0.0F, 0.0F, static_cast<float>(_impl->Width), static_cast<float>(_impl->Height)});
}

void WindowUi::DrawTexture(const Texture& texture, const Sampler& sampler,
    std::span<const WindowQuadVertex, 4> strip, bool premultiplied)
{
    auto& commands = *_impl->Commands;
    commands.SetPipeline(premultiplied ? *_impl->Premultiplied : *_impl->Opaque);
    auto& program = _impl->Program(SceneProgram::Composite);
    const float one = 1.0F;
    const std::int32_t no = 0;
    const float none[4]{};
    program.Write("alpha", &one, 4, SceneShaderAbi::ValueType::Float);
    program.Write("use_mask", &no, 4, SceneShaderAbi::ValueType::Int);
    program.Write("fade_color", none, 16, SceneShaderAbi::ValueType::Vec4);
    commands.BindSampledTexture(0, &texture, &sampler);
    commands.BindSampledTexture(1, nullptr, nullptr);
    _impl->Quad(strip);
    commands.BindSampledTexture(0, nullptr, nullptr);
}

void WindowUi::DrawBackdrop(const Texture& photo, const Sampler& sampler,
    std::span<const WindowQuadVertex, 4> strip, float strength, float seconds)
{
    auto& commands = *_impl->Commands;
    commands.SetPipeline(*_impl->Backdrop);
    auto& program = _impl->Program(SceneProgram::Backdrop);
    const float width = static_cast<float>(_impl->Width);
    const float height = static_cast<float>(_impl->Height);
    program.Write("strength", &strength, 4, SceneShaderAbi::ValueType::Float);
    program.Write("time", &seconds, 4, SceneShaderAbi::ValueType::Float);
    program.Write("view_width", &width, 4, SceneShaderAbi::ValueType::Float);
    program.Write("view_height", &height, 4, SceneShaderAbi::ValueType::Float);
    commands.BindSampledTexture(0, &photo, &sampler);
    commands.BindSampledTexture(1, _impl->Noise.Texture.get(), _impl->Noise.Sampler.get());
    _impl->Quad(strip);
    commands.BindSampledTexture(1, nullptr, nullptr);
    commands.BindSampledTexture(0, nullptr, nullptr);
}

void WindowUi::End()
{
    _impl->Commands->EndRendering();
}

class VulkanTransientGeometry final : public MphRead::TransientGeometryResource
{
public:
    VulkanTransientGeometry(std::shared_ptr<VulkanDeviceState> device, VulkanCommandList& commands)
        : _device(std::move(device)), _commands(commands) {}

    void BeginFrame() override {}

    void Draw(MphRead::TransientPrimitiveTopology topology, std::span<const MphRead::TransientVertex> vertices,
        bool hasTexCoords) override
    {
        using T = MphRead::TransientPrimitiveTopology;
        if (vertices.empty()) return;
        const std::size_t n = vertices.size();
        auto& sequence = _sequence;
        if (n > sequence.capacity()) FrameTelemetry::Count(FrameTelemetry::Counter::VectorGrowth);
        sequence.resize(n);
        MphRead::BuildTransientIndexSequence(sequence);
        auto& indices = _indices;
        indices.clear();
        const bool lines = topology == T::LineLoop;
        const auto previousCapacity = indices.capacity();
        MphRead::AppendTransientDrawIndices(indices, sequence, topology);
        if (indices.capacity() != previousCapacity) FrameTelemetry::Count(FrameTelemetry::Counter::VectorGrowth);
        if (indices.empty()) return;
        auto positions = _commands.Allocate(n * 24U);
        auto* out = reinterpret_cast<float*>(positions.Data);
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto& v = vertices[i];
            const float data[6]{v.Position.X, v.Position.Y, v.Position.Z, v.TexCoord.X, v.TexCoord.Y, v.TexCoord.Z};
            std::memcpy(out + i * 6U, data, sizeof(data));
        }
        auto index = _commands.Allocate(indices.size() * 4U, 4);
        std::memcpy(index.Data, indices.data(), indices.size() * 4U);
        const auto constant = [&](const float* value, std::size_t count) {
            auto slice = _commands.Allocate(16);
            std::memcpy(slice.Data, value, count * sizeof(float));
            return VulkanCommandList::SceneStream{slice.Buffer, slice.Offset, 0};
        };
        VulkanCommandList::SceneDraw draw{};
        draw.Streams[0] = {positions.Buffer, positions.Offset, 24};
        draw.Streams[1] = constant(_device->CurrentNormal.data(), 3);
        draw.Streams[2] = constant(_device->CurrentColor.data(), 4);
        draw.Streams[3] = hasTexCoords ? VulkanCommandList::SceneStream{positions.Buffer, positions.Offset + 12, 24}
            : constant(_device->CurrentTexCoord.data(), 3);
        draw.IndexBuffer = index.Buffer;
        draw.IndexOffset = index.Offset;
        draw.IndexCount = static_cast<std::uint32_t>(indices.size());
        draw.Lines = lines;
        _commands.DrawScene(draw);
    }

private:
    std::shared_ptr<VulkanDeviceState> _device;
    VulkanCommandList& _commands;
    // Uploads copy into submission-owned storage before Draw returns. Retain
    // index generation scratch; it is never referenced by queued GPU work.
    std::vector<std::uint32_t> _sequence, _indices;
};
