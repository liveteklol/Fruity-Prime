// Included inside the Vulkan backend after its native shader/layout classes.
struct VulkanPipelineKey final
{
    GraphicsPipelineDesc state;
    ShaderDesc vertex, fragment;
    explicit VulkanPipelineKey(const GraphicsPipelineDesc& desc)
        : state(desc), vertex(desc.vertexShader->Desc()), fragment(desc.fragmentShader->Desc())
    {
        state.vertexShader = nullptr; state.fragmentShader = nullptr;
    }
    bool operator==(const VulkanPipelineKey&) const = default;
    [[nodiscard]] std::size_t Hash() const
    {
        std::size_t result = 0;
        auto add = [&](auto value) {
            using T = decltype(value);
            std::size_t hash;
            if constexpr (std::is_enum_v<T>) hash = std::hash<std::underlying_type_t<T>>{}(static_cast<std::underlying_type_t<T>>(value));
            else hash = std::hash<T>{}(value);
            result ^= hash + 0x9e3779b9U + (result << 6) + (result >> 2);
        };
        for (const auto* shader : {&vertex, &fragment})
        {
            add(shader->stage); add(shader->format); add(std::string_view(shader->entryPoint));
            add(std::string_view(reinterpret_cast<const char*>(shader->code.data()), shader->code.size()));
        }
        add(state.pipelineLayout.groups.size());
        for (const auto& group : state.pipelineLayout.groups)
        {
            add(group.entries.size());
            for (const auto& item : group.entries) { add(item.binding); add(item.type); add(item.stages); add(item.count); }
        }
        add(state.topology); add(state.sampleCount); add(state.alphaTest); add(state.depthStencilFormat); add(state.smallConstantBytes);
        const auto& r = state.rasterizer;
        add(r.cullMode); add(r.frontFace); add(r.fillMode); add(r.depthClampEnable); add(r.depthBiasEnable);
        add(r.depthBiasConstant); add(r.depthBiasSlope); add(r.lineWidth);
        const auto& d = state.depthStencil;
        add(d.depthTestEnable); add(d.depthWriteEnable); add(d.depthCompareOp); add(d.stencilTestEnable);
        add(d.stencilReadMask); add(d.stencilWriteMask);
        for (const auto* face : {&d.front, &d.back}) { add(face->failOp); add(face->depthFailOp); add(face->passOp); add(face->compareOp); }
        add(state.vertexBuffers.size());
        for (const auto& item : state.vertexBuffers) { add(item.slot); add(item.stride); add(item.inputRate); }
        add(state.vertexAttributes.size());
        for (const auto& item : state.vertexAttributes) { add(item.location); add(item.bufferSlot); add(item.format); add(item.offset); }
        add(state.blendAttachments.size());
        for (const auto& item : state.blendAttachments)
        {
            add(item.blendEnable); add(item.srcColorFactor); add(item.dstColorFactor); add(item.colorOp);
            add(item.srcAlphaFactor); add(item.dstAlphaFactor); add(item.alphaOp); add(item.writeMask);
        }
        add(state.colorFormats.size()); for (const auto item : state.colorFormats) add(item);
        return result;
    }
};

class VulkanGraphicsPipeline final : public GraphicsPipeline, public VulkanNativeOwner
{
public:
    // A scene pass as the renderer describes it: state with no binding
    // layout, formats that may not be the attachments', and possibly no
    // program. The scene command list builds the native pipeline per draw.
    struct Deferred final {};
    VulkanGraphicsPipeline(std::shared_ptr<VulkanDeviceState> device, const GraphicsPipelineDesc& desc, Deferred)
        : _device(std::move(device)), _registration(*_device, *this), _desc(desc), _deferred(true) {}
    // scene: the stencil reference and vertex strides are dynamic, as the
    // scene command list sets them per draw.
    VulkanGraphicsPipeline(std::shared_ptr<VulkanDeviceState> device, const GraphicsPipelineDesc& desc,
        bool scene = false)
        : _device(std::move(device)), _registration(*_device, *this), _desc(desc)
    {
        auto& vk = *_device->ContextPointer->_impl;
        VkPhysicalDeviceProperties properties{};
        vk.vkGetPhysicalDeviceProperties(vk.physical, &properties);
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        vk.vkGetPhysicalDeviceFeatures2(vk.physical, &features);
        const auto* vertex = dynamic_cast<const VulkanShader*>(desc.vertexShader);
        const auto* fragment = dynamic_cast<const VulkanShader*>(desc.fragmentShader);
        if (!vertex || !fragment || vertex->DeviceState() != _device
            || fragment->DeviceState() != _device
            || vertex->Desc().stage != ShaderStage::Vertex || fragment->Desc().stage != ShaderStage::Fragment)
            throw std::invalid_argument("Vulkan RHI: pipeline shader/layout mismatch.");
        if (desc.colorFormats.size() != desc.blendAttachments.size()
            || desc.colorFormats.size() > vk.caps.maxColorAttachments)
            throw std::invalid_argument("Vulkan RHI: pipeline color attachments mismatch.");
        if ((desc.rasterizer.depthClampEnable && !vk.caps.supportsDepthClamp)
            || (desc.rasterizer.fillMode == FillMode::Wireframe && !vk.caps.supportsWireframe)
            || (desc.rasterizer.lineWidth != 1.0F && !features.features.wideLines)
            || !std::isfinite(desc.rasterizer.lineWidth)
            || desc.rasterizer.lineWidth < properties.limits.lineWidthRange[0]
            || desc.rasterizer.lineWidth > properties.limits.lineWidthRange[1])
            throw std::invalid_argument("Vulkan RHI: pipeline rasterization feature unavailable.");
        if (desc.sampleCount != 1 && desc.sampleCount != 2 && desc.sampleCount != 4
            && desc.sampleCount != 8 && desc.sampleCount != 16 && desc.sampleCount != 32 && desc.sampleCount != 64)
            throw std::invalid_argument("Vulkan RHI: invalid pipeline sample count.");
        const auto samples = static_cast<VkSampleCountFlags>(desc.sampleCount);
        if ((!desc.colorFormats.empty() && !(properties.limits.framebufferColorSampleCounts & samples))
            || (desc.depthStencilFormat != TextureFormat::Undefined && !(properties.limits.framebufferDepthSampleCounts & samples)))
            throw std::invalid_argument("Vulkan RHI: unsupported pipeline sample count.");
        if (desc.vertexBuffers.size() > properties.limits.maxVertexInputBindings
            || desc.vertexAttributes.size() > properties.limits.maxVertexInputAttributes)
            throw std::invalid_argument("Vulkan RHI: too many vertex declarations.");
        if (desc.rasterizer.cullMode > CullMode::Back || desc.rasterizer.frontFace > FrontFace::CounterClockwise
            || desc.rasterizer.fillMode > FillMode::Wireframe || desc.alphaTest > AlphaTestMode::LessThanOne
            || desc.depthStencil.depthCompareOp > CompareOp::Always)
            throw std::invalid_argument("Vulkan RHI: invalid pipeline state enum.");
        if (!std::isfinite(desc.rasterizer.depthBiasConstant) || !std::isfinite(desc.rasterizer.depthBiasSlope))
            throw std::invalid_argument("Vulkan RHI: non-finite depth bias.");
        auto validateStencil = [](const StencilFaceStateDesc& face) {
            if (face.failOp > StencilOp::DecrementWrap || face.passOp > StencilOp::DecrementWrap
                || face.depthFailOp > StencilOp::DecrementWrap || face.compareOp > CompareOp::Always)
                throw std::invalid_argument("Vulkan RHI: invalid stencil state enum.");
        };
        validateStencil(desc.depthStencil.front); validateStencil(desc.depthStencil.back);
        if ((desc.depthStencil.depthTestEnable || desc.depthStencil.depthWriteEnable
            || desc.depthStencil.stencilTestEnable) && desc.depthStencilFormat == TextureFormat::Undefined)
            throw std::invalid_argument("Vulkan RHI: depth/stencil state needs an attachment format.");
        if (desc.depthStencilFormat != TextureFormat::Undefined)
        {
            if (desc.depthStencilFormat != TextureFormat::D16Unorm && desc.depthStencilFormat != TextureFormat::D32Float
                && desc.depthStencilFormat != TextureFormat::D24UnormS8Uint && desc.depthStencilFormat != TextureFormat::D32FloatS8Uint)
                throw std::invalid_argument("Vulkan RHI: color format used as depth attachment.");
            VkFormatProperties format{};
            vk.vkGetPhysicalDeviceFormatProperties(vk.physical, VulkanResources::Format(desc.depthStencilFormat), &format);
            if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
                throw std::invalid_argument("Vulkan RHI: unsupported depth attachment format.");
            if (desc.depthStencil.stencilTestEnable && desc.depthStencilFormat != TextureFormat::D24UnormS8Uint
                && desc.depthStencilFormat != TextureFormat::D32FloatS8Uint)
                throw std::invalid_argument("Vulkan RHI: stencil test needs a stencil format.");
        }
        for (std::size_t i = 0; i < desc.blendAttachments.size(); ++i)
        {
            const auto& item = desc.blendAttachments[i];
            if (item.colorOp > BlendOp::Max || item.alphaOp > BlendOp::Max
                || (static_cast<unsigned>(item.writeMask) & ~15U))
                throw std::invalid_argument("Vulkan RHI: invalid blend state.");
            if (i && !features.features.independentBlend && item != desc.blendAttachments.front())
                throw std::invalid_argument("Vulkan RHI: independent blending unavailable.");
            VkFormatProperties format{};
            vk.vkGetPhysicalDeviceFormatProperties(vk.physical, VulkanResources::Format(desc.colorFormats[i]), &format);
            auto needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
            if (!(format.optimalTilingFeatures & needed)
                || (item.blendEnable && !(format.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)))
                throw std::invalid_argument("Vulkan RHI: unsupported color attachment format.");
        }
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        for (const auto& item : desc.vertexBuffers)
        {
            if (item.slot >= properties.limits.maxVertexInputBindings
                || item.stride > properties.limits.maxVertexInputBindingStride || item.inputRate > VertexInputRate::Instance)
                throw std::invalid_argument("Vulkan RHI: invalid vertex buffer declaration.");
            for (const auto& previous : vertexBindings)
                if (previous.binding == item.slot) throw std::invalid_argument("Vulkan RHI: duplicate vertex binding.");
            vertexBindings.push_back({item.slot, item.stride,
                item.inputRate == VertexInputRate::Instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
        }
        std::vector<VkVertexInputAttributeDescription> attributes;
        for (const auto& item : desc.vertexAttributes)
        {
            if (item.location >= properties.limits.maxVertexInputAttributes
                || item.offset > properties.limits.maxVertexInputAttributeOffset)
                throw std::invalid_argument("Vulkan RHI: vertex attribute exceeds device limits.");
            VkFormatProperties format{};
            vk.vkGetPhysicalDeviceFormatProperties(vk.physical, ToVkVertexFormat(item.format), &format);
            if (!(format.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
                throw std::invalid_argument("Vulkan RHI: unsupported vertex format.");
            const auto found = std::find_if(vertexBindings.begin(), vertexBindings.end(),
                [&](const auto& binding) { return binding.binding == item.bufferSlot; });
            if (found == vertexBindings.end()) throw std::invalid_argument("Vulkan RHI: missing vertex binding.");
            for (const auto& previous : attributes)
                if (previous.location == item.location) throw std::invalid_argument("Vulkan RHI: duplicate vertex location.");
            attributes.push_back({item.location, item.bufferSlot, ToVkVertexFormat(item.format), item.offset});
        }
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertexBindings.size()); input.pVertexBindingDescriptions = vertexBindings.data();
        input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()); input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = ToVkTopology(desc.topology);
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1; viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.depthClampEnable = desc.rasterizer.depthClampEnable;
        raster.polygonMode = desc.rasterizer.fillMode == FillMode::Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        raster.cullMode = desc.rasterizer.cullMode == CullMode::None ? VK_CULL_MODE_NONE
            : desc.rasterizer.cullMode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
        raster.frontFace = desc.rasterizer.frontFace == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.depthBiasEnable = desc.rasterizer.depthBiasEnable;
        raster.depthBiasConstantFactor = desc.rasterizer.depthBiasConstant; raster.depthBiasSlopeFactor = desc.rasterizer.depthBiasSlope;
        raster.lineWidth = desc.rasterizer.lineWidth;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
        auto stencil = [&](const StencilFaceStateDesc& face) {
            return VkStencilOpState{static_cast<VkStencilOp>(face.failOp), static_cast<VkStencilOp>(face.passOp),
                static_cast<VkStencilOp>(face.depthFailOp), static_cast<VkCompareOp>(face.compareOp),
                desc.depthStencil.stencilReadMask, desc.depthStencil.stencilWriteMask, 0};
        };
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = desc.depthStencil.depthTestEnable; depth.depthWriteEnable = desc.depthStencil.depthWriteEnable;
        depth.depthCompareOp = static_cast<VkCompareOp>(desc.depthStencil.depthCompareOp);
        depth.stencilTestEnable = desc.depthStencil.stencilTestEnable;
        depth.front = stencil(desc.depthStencil.front); depth.back = stencil(desc.depthStencil.back);
        std::vector<VkPipelineColorBlendAttachmentState> blends;
        for (const auto& item : desc.blendAttachments)
            blends.push_back({item.blendEnable, ToVkBlendFactor(item.srcColorFactor), ToVkBlendFactor(item.dstColorFactor),
                static_cast<VkBlendOp>(item.colorOp), ToVkBlendFactor(item.srcAlphaFactor), ToVkBlendFactor(item.dstAlphaFactor),
                static_cast<VkBlendOp>(item.alphaOp), static_cast<VkColorComponentFlags>(item.writeMask)});
        for (std::size_t i = 0; i < blends.size(); ++i)
            if (IsRgbTextureFormat(desc.colorFormats[i]))
                blends[i].colorWriteMask &= VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = static_cast<std::uint32_t>(blends.size()); blend.pAttachments = blends.data();
        std::vector<VkDynamicState> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        if (scene)
        {
            dynamicStates.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
            dynamicStates.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE);
        }
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()); dynamic.pDynamicStates = dynamicStates.data();
        if (desc.pipelineLayout.groups.empty() || desc.pipelineLayout.groups.size() > vk.caps.maxBindingGroups)
            throw std::invalid_argument("Vulkan RHI: pipeline binding group count exceeds device limits.");
        std::vector<VkDescriptorSetLayout> layouts;
        for (const auto& group : desc.pipelineLayout.groups)
        {
            _bindingOwners.push_back(std::make_shared<VulkanBindingLayout>(_device, group));
            layouts.push_back(_bindingOwners.back()->Native());
        }
        VkPipelineLayoutCreateInfo layoutCreate{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutCreate.setLayoutCount = static_cast<std::uint32_t>(layouts.size()); layoutCreate.pSetLayouts = layouts.data();
        if (desc.smallConstantBytes > 128 || desc.smallConstantBytes % 4)
            throw std::invalid_argument("Vulkan RHI: invalid portable small constant range.");
        const VkPushConstantRange small{VK_SHADER_STAGE_ALL_GRAPHICS, 0, desc.smallConstantBytes};
        layoutCreate.pushConstantRangeCount = desc.smallConstantBytes ? 1 : 0;
        layoutCreate.pPushConstantRanges = desc.smallConstantBytes ? &small : nullptr;
        Check(vk.vkCreatePipelineLayout(vk.device, &layoutCreate, nullptr, &_layout), "vkCreatePipelineLayout");
        try
        {
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex->Native(), vertex->Desc().entryPoint.c_str(), nullptr};
            stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment->Native(), fragment->Desc().entryPoint.c_str(), nullptr};
            std::vector<VkFormat> formats;
            for (const auto format : desc.colorFormats) formats.push_back(VulkanResources::Format(format));
            VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
            rendering.colorAttachmentCount = static_cast<std::uint32_t>(formats.size()); rendering.pColorAttachmentFormats = formats.data();
            rendering.depthAttachmentFormat = desc.depthStencilFormat == TextureFormat::Undefined
                ? VK_FORMAT_UNDEFINED : VulkanResources::Format(desc.depthStencilFormat);
            if (desc.depthStencilFormat == TextureFormat::D24UnormS8Uint || desc.depthStencilFormat == TextureFormat::D32FloatS8Uint)
                rendering.stencilAttachmentFormat = rendering.depthAttachmentFormat;
            VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            create.pNext = &rendering; create.stageCount = 2; create.pStages = stages.data();
            create.pVertexInputState = &input; create.pInputAssemblyState = &assembly; create.pViewportState = &viewport;
            create.pRasterizationState = &raster; create.pMultisampleState = &multisample; create.pDepthStencilState = &depth;
            create.pColorBlendState = &blend; create.pDynamicState = &dynamic; create.layout = _layout;
            const auto result = _device->PipelineCache
                ? _device->PipelineCache->CreatePipeline(create, _pipeline)
                : vk.vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &create, nullptr, &_pipeline);
            Check(result, "vkCreateGraphicsPipelines");
            ++_device->Programs;
            _desc.vertexShader = _desc.fragmentShader = nullptr;
        }
        catch (...) { if (_pipeline) vk.vkDestroyPipeline(vk.device, _pipeline, nullptr); vk.vkDestroyPipelineLayout(vk.device, _layout, nullptr); throw; }
    }
    VulkanGraphicsPipeline(const GraphicsPipelineDesc& desc, std::shared_ptr<VulkanGraphicsPipeline> owner)
        : _device(owner->_device), _registration(*_device, *this), _desc(desc), _pipeline(owner->_pipeline), _layout(owner->_layout), _owner(std::move(owner))
    { _desc.vertexShader = _desc.fragmentShader = nullptr; }
    ~VulkanGraphicsPipeline() override { CloseNative(); }
    void CloseNative() noexcept override
    {
        if (!_device->ContextPointer) return;
        if (!_device->Closing) { try { _device->ForgetScene(this); } catch (...) {} }
        if (_owner || _deferred) { _pipeline = VK_NULL_HANDLE; _layout = VK_NULL_HANDLE; return; }
        if (!_pipeline) return;
        try { _device->FlushScene(); } catch (...) {}
        auto& vk = *_device->ContextPointer->_impl;
        _device->Retire([device = vk.device, destroyPipeline = vk.vkDestroyPipeline,
            destroyLayout = vk.vkDestroyPipelineLayout, pipeline = _pipeline, layout = _layout] {
            destroyPipeline(device, pipeline, nullptr);
            destroyLayout(device, layout, nullptr);
        });
        --_device->Programs;
        _pipeline = VK_NULL_HANDLE; _layout = VK_NULL_HANDLE;
    }
    [[nodiscard]] const GraphicsPipelineDesc& Desc() const noexcept override { return _desc; }
    [[nodiscard]] VkPipeline Native() const noexcept { return _owner ? _owner->Native() : _pipeline; }
    [[nodiscard]] VkPipelineLayout Layout() const noexcept { return _owner ? _owner->Layout() : _layout; }
    [[nodiscard]] std::shared_ptr<VulkanBindingLayout> BindingLayoutOwner(std::uint32_t slot) const
    { return _owner ? _owner->BindingLayoutOwner(slot) : _bindingOwners.at(slot); }
    [[nodiscard]] const std::shared_ptr<VulkanDeviceState>& DeviceState() const noexcept { return _device; }
    [[nodiscard]] bool IsDeferred() const noexcept { return _deferred; }
private:
    std::vector<std::shared_ptr<VulkanBindingLayout>> _bindingOwners;
    bool _deferred = false;
    std::shared_ptr<VulkanDeviceState> _device;
    VulkanNativeRegistration _registration;
    GraphicsPipelineDesc _desc;
    VkPipeline _pipeline = VK_NULL_HANDLE;
    VkPipelineLayout _layout = VK_NULL_HANDLE;
    std::shared_ptr<VulkanGraphicsPipeline> _owner;
};
