#include "SceneRenderer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QtMath>


#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace fp {

namespace {

struct FrameUniforms {
    float proj[16];
    float view[16];
    float billboardSphere[16];
    float billboardCylinder[16];
    float light1Vec[4];
    float light1Col[4];
    float light2Vec[4];
    float light2Col[4];
    float fogColor[4];
    float fogParams[4];
};

struct DrawConstants {
    float texMtx[16];
    float diffuse[4];
    float ambient[4];
    float specular[4];
    int32_t flags[4];
};
static_assert(sizeof(DrawConstants) == 128, "push constants must fit the 128-byte guaranteed minimum");

constexpr float kNearClip = 0.0625f;
constexpr float kFarClip = 10000.0f;

void copyMatrix(float* dst, const QMatrix4x4& m) { std::memcpy(dst, m.constData(), 16 * sizeof(float)); }

VkSamplerAddressMode addressMode(RepeatMode mode)
{
    switch (mode) {
    case RepeatMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RepeatMode::Mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case RepeatMode::Clamp:
    default:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

void check(VkResult result, const char* what)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: VkResult " + std::to_string(result));
    }
}

} // namespace

QVector3D Camera::forward() const
{
    const float yawR = qDegreesToRadians(yaw);
    const float pitchR = qDegreesToRadians(pitch);
    return QVector3D(-std::sin(yawR) * std::cos(pitchR), std::sin(pitchR), -std::cos(yawR) * std::cos(pitchR));
}

QVector3D Camera::right() const
{
    const float yawR = qDegreesToRadians(yaw);
    return QVector3D(std::cos(yawR), 0.0f, -std::sin(yawR));
}

QMatrix4x4 Camera::view() const
{
    QMatrix4x4 m;
    if (useTarget) {
        m.lookAt(position, target, up);
    } else {
        m.lookAt(position, position + forward(), QVector3D(0, 1, 0));
    }
    return m;
}

SceneRenderer::Buffer SceneRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryIndex, bool map)
{
    Buffer out;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    check(m_dev->vkCreateBuffer(m_window->device(), &info, nullptr, &out.buffer), "vkCreateBuffer");
    VkMemoryRequirements req;
    m_dev->vkGetBufferMemoryRequirements(m_window->device(), out.buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryIndex;
    check(m_dev->vkAllocateMemory(m_window->device(), &alloc, nullptr, &out.memory), "vkAllocateMemory");
    check(m_dev->vkBindBufferMemory(m_window->device(), out.buffer, out.memory, 0), "vkBindBufferMemory");
    if (map) {
        check(m_dev->vkMapMemory(m_window->device(), out.memory, 0, size, 0, &out.mapped), "vkMapMemory");
    }
    return out;
}

void SceneRenderer::destroyBuffer(Buffer& buffer)
{
    if (buffer.buffer) {
        m_dev->vkDestroyBuffer(m_window->device(), buffer.buffer, nullptr);
    }
    if (buffer.mapped) {
        m_dev->vkUnmapMemory(m_window->device(), buffer.memory);
    }
    if (buffer.memory) {
        m_dev->vkFreeMemory(m_window->device(), buffer.memory, nullptr);
    }
    buffer = {};
}

SceneRenderer::Texture SceneRenderer::createTexture(const Image& image)
{
    VkDevice device = m_window->device();
    Texture tex;
    const VkDeviceSize size = image.rgba.size() * sizeof(uint32_t);

    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex(), false);
    void* mapped = nullptr;
    check(m_dev->vkMapMemory(device, staging.memory, 0, size, 0, &mapped), "vkMapMemory");
    std::memcpy(mapped, image.rgba.data(), size);
    m_dev->vkUnmapMemory(device, staging.memory);

    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(m_dev->vkCreateImage(device, &info, nullptr, &tex.image), "vkCreateImage");

    VkMemoryRequirements req;
    m_dev->vkGetImageMemoryRequirements(device, tex.image, &req);
    uint32_t memoryIndex = m_window->deviceLocalMemoryIndex();
    if (!(req.memoryTypeBits & (1u << memoryIndex))) {
        VkPhysicalDeviceMemoryProperties props;
        m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &props);
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if (req.memoryTypeBits & (1u << i)) {
                memoryIndex = i;
                break;
            }
        }
    }
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryIndex;
    check(m_dev->vkAllocateMemory(device, &alloc, nullptr, &tex.memory), "vkAllocateMemory");
    check(m_dev->vkBindImageMemory(device, tex.image, tex.memory, 0), "vkBindImageMemory");

    VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbInfo.commandPool = m_window->graphicsCommandPool();
    cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = 1;
    VkCommandBuffer cb;
    check(m_dev->vkAllocateCommandBuffers(device, &cbInfo, &cb), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_dev->vkBeginCommandBuffer(cb, &begin);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    m_dev->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
        nullptr, 1, &barrier);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = info.extent;
    m_dev->vkCmdCopyBufferToImage(cb, staging.buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    m_dev->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
        nullptr, 0, nullptr, 1, &barrier);
    m_dev->vkEndCommandBuffer(cb);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    check(m_dev->vkQueueSubmit(m_window->graphicsQueue(), 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    m_dev->vkQueueWaitIdle(m_window->graphicsQueue());
    m_dev->vkFreeCommandBuffers(device, m_window->graphicsCommandPool(), 1, &cb);
    destroyBuffer(staging);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = info.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(m_dev->vkCreateImageView(device, &viewInfo, nullptr, &tex.view), "vkCreateImageView");
    return tex;
}

VkShaderModule SceneRenderer::loadShader(const QString& resource)
{
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("missing shader " + resource.toStdString());
    }
    const QByteArray code = file.readAll();
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = static_cast<size_t>(code.size());
    info.pCode = reinterpret_cast<const uint32_t*>(code.constData());
    VkShaderModule module;
    check(m_dev->vkCreateShaderModule(m_window->device(), &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

VkSampler SceneRenderer::samplerFor(RepeatMode x, RepeatMode y)
{
    const int key = static_cast<int>(x) * 3 + static_cast<int>(y);
    if (auto it = m_samplers.find(key); it != m_samplers.end()) {
        return it->second;
    }
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = addressMode(x);
    info.addressModeV = addressMode(y);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.25f;
    VkSampler sampler;
    check(m_dev->vkCreateSampler(m_window->device(), &info, nullptr, &sampler), "vkCreateSampler");
    m_samplers[key] = sampler;
    return sampler;
}

void SceneRenderer::createPipelines()
{
    VkDevice device = m_window->device();
    VkShaderModule vert = loadShader(QStringLiteral(":/shaders/room.vert.spv"));
    VkShaderModule frag = loadShader(QStringLiteral(":/shaders/room.frag.spv"));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {4, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, matrixId)},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 5;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = m_window->sampleCountFlagBits();

    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;

    const VkCullModeFlags cullModes[3] = {VK_CULL_MODE_NONE, VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT};

    for (int pass = 0; pass < PassCount; pass++) {
        for (int cull = 0; cull < 3; cull++) {
            VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            rs.polygonMode = VK_POLYGON_MODE_FILL;
            rs.cullMode = cullModes[cull];
            rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rs.lineWidth = 1.0f;
            if (pass == PassDecal) {
                rs.depthBiasEnable = VK_TRUE;
                rs.depthBiasConstantFactor = -1.0f;
                rs.depthBiasSlopeFactor = -1.0f;
            }

            VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            ds.depthTestEnable = VK_TRUE;
            ds.depthWriteEnable = pass == PassTranslucent ? VK_FALSE : VK_TRUE;
            ds.depthCompareOp = pass == PassOpaque ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_LESS_OR_EQUAL;

            VkPipelineColorBlendAttachmentState att{};
            att.colorWriteMask = 0xF;
            if (pass != PassOpaque) {
                att.blendEnable = VK_TRUE;
                att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                att.colorBlendOp = VK_BLEND_OP_ADD;
                att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                att.alphaBlendOp = VK_BLEND_OP_ADD;
            }
            VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            cb.attachmentCount = 1;
            cb.pAttachments = &att;

            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount = 2;
            info.pStages = stages;
            info.pVertexInputState = &vertexInput;
            info.pInputAssemblyState = &ia;
            info.pViewportState = &vp;
            info.pRasterizationState = &rs;
            info.pMultisampleState = &ms;
            info.pDepthStencilState = &ds;
            info.pColorBlendState = &cb;
            info.pDynamicState = &dyn;
            info.layout = m_pipelineLayout;
            info.renderPass = m_window->defaultRenderPass();
            check(m_dev->vkCreateGraphicsPipelines(device, m_pipelineCache, 1, &info, nullptr, &m_pipelines[pass][cull]),
                "vkCreateGraphicsPipelines");
        }
    }
    m_dev->vkDestroyShaderModule(device, vert, nullptr);
    m_dev->vkDestroyShaderModule(device, frag, nullptr);
}

SceneRenderer::SceneRenderer(VulkanWindow* window, std::unique_ptr<Scene> scene)
    : m_window(window)
    , m_scene(std::move(scene))
{
}

VkDescriptorSet SceneRenderer::textureSetFor(const Model* model, int recolor, int textureId, int paletteId, RepeatMode x, RepeatMode y)
{
    const bool textured = textureId >= 0 && textureId < static_cast<int>(model->textureCount());
    const int samplerKey = textured ? static_cast<int>(x) * 3 + static_cast<int>(y) : 0;
    if (!textured) {
        textureId = paletteId = -1;
    }
    const auto setKey = std::make_tuple(textured ? model : nullptr, textured ? recolor : 0, textureId, paletteId, samplerKey);
    if (auto it = m_textureSets.find(setKey); it != m_textureSets.end()) {
        return it->second;
    }
    VkImageView view = m_whiteTexture.view;
    if (textured) {
        const auto texKey = std::make_tuple(model, recolor, textureId, paletteId);
        auto it = m_textures.find(texKey);
        if (it == m_textures.end()) {
            it = m_textures.emplace(texKey, createTexture(model->decodeTexture(textureId, paletteId, recolor))).first;
        }
        view = it->second.view;
    }
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = m_descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_textureLayout;
    VkDescriptorSet set;
    check(m_dev->vkAllocateDescriptorSets(m_window->device(), &alloc, &set), "vkAllocateDescriptorSets");
    VkDescriptorImageInfo imageInfo{textured ? samplerFor(x, y) : samplerFor(RepeatMode::Clamp, RepeatMode::Clamp), view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    m_dev->vkUpdateDescriptorSets(m_window->device(), 1, &write, 0, nullptr);
    m_textureSets[setKey] = set;
    return set;
}

void SceneRenderer::initResources()
{
    VkDevice device = m_window->device();
    m_dev = m_window->vulkanInstance()->deviceFunctions(device);
    m_startNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

    uploadVertices();

    const int frames = m_window->concurrentFrameCount();
    for (int i = 0; i < frames; i++) {
        m_uniformBuffers[i] = createBuffer(
            sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_window->hostVisibleMemoryIndex(), true);
        m_matrixBuffers[i] = createBuffer(
            kMaxMatrices * sizeof(Mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, m_window->hostVisibleMemoryIndex(), true);
    }

    VkDescriptorSetLayoutBinding frameBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = frameBindings;
    check(m_dev->vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_frameLayout), "frame layout");
    VkDescriptorSetLayoutBinding texBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &texBinding;
    check(m_dev->vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_textureLayout), "texture layout");

    const uint32_t maxTextureSets = 8192;
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(frames)},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(frames)},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxTextureSets},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // the screens' image is freed on a resize
    poolInfo.maxSets = static_cast<uint32_t>(frames) + maxTextureSets;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    check(m_dev->vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool), "descriptor pool");

    for (int i = 0; i < frames; i++) {
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = m_descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_frameLayout;
        check(m_dev->vkAllocateDescriptorSets(device, &alloc, &m_frameSets[i]), "frame set");
        VkDescriptorBufferInfo uboInfo{m_uniformBuffers[i].buffer, 0, sizeof(FrameUniforms)};
        VkDescriptorBufferInfo ssboInfo{m_matrixBuffers[i].buffer, 0, kMaxMatrices * sizeof(Mat4)};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_frameSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &uboInfo;
        writes[1] = writes[0];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &ssboInfo;
        m_dev->vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

    VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawConstants)};
    VkDescriptorSetLayout setLayouts[] = {m_frameLayout, m_textureLayout};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plInfo.setLayoutCount = 2;
    plInfo.pSetLayouts = setLayouts;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    check(m_dev->vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout), "pipeline layout");
    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    check(m_dev->vkCreatePipelineCache(device, &cacheInfo, nullptr, &m_pipelineCache), "pipeline cache");
    createPipelines();
    createHudPipeline();

    Image white;
    white.width = white.height = 1;
    white.rgba = {0xFFFFFFFFu};
    m_whiteTexture = createTexture(white);

    QElapsedTimer timer;
    timer.start();
    collectDraws(0.0); // uploads every texture a draw needs
    // Renderer.InitTextures: every texture an animation can switch to, up front.
    for (const ModelInstance& inst : m_scene->instances) {
        const AnimationSet& anims = inst.model->animations();
        for (const TextureAnimationGroup& group : anims.texture) {
            for (const Material& material : inst.model->materials()) {
                auto it = group.animations.find(material.name);
                if (it == group.animations.end()) {
                    continue;
                }
                for (int j = it->second.startIndex; j < it->second.startIndex + it->second.count; j++) {
                    if (j < static_cast<int>(group.textureIds.size()) && j < static_cast<int>(group.paletteIds.size())) {
                        textureSetFor(inst.model, inst.recolor, group.textureIds[j], group.paletteIds[j], material.xRepeat, material.yRepeat);
                    }
                }
            }
        }
    }
    size_t instances = m_scene->instances.size();
    qInfo("device \"%s\", swapchain format %d, present mode %s", qPrintable(m_window->deviceName()), m_window->colorFormat(),
        m_window->presentModeName());
    qInfo("room \"%s\": %zu model instances, %zu models, %zu textures uploaded in %lld ms", m_scene->room ? m_scene->room->name : "(none)", instances,
        m_scene->models.size(), m_textures.size(), static_cast<long long>(timer.elapsed()));
    placeCameraInRoom();
    if (m_initialCamera) {
        m_camera = *m_initialCamera;
    }
}

void SceneRenderer::uploadVertices()
{
    // Every model's vertices in one buffer; each model draws from its base.
    // Models loaded later (a bomb, a hunter joining online) come in by
    // building it again; the frames in flight finish with the old one first.
    if (m_vertexBuffer.buffer != VK_NULL_HANDLE) {
        m_dev->vkDeviceWaitIdle(m_window->device());
        destroyBuffer(m_vertexBuffer);
    }
    m_baseVertex.clear();
    std::vector<Vertex> vertices;
    for (const auto& model : m_scene->models) {
        m_baseVertex[model.get()] = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), model->vertices().begin(), model->vertices().end());
    }
    const VkDeviceSize vbSize = std::max<VkDeviceSize>(1, vertices.size() * sizeof(Vertex));
    m_vertexBuffer = createBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_window->hostVisibleMemoryIndex(), true);
    std::memcpy(m_vertexBuffer.mapped, vertices.data(), vertices.size() * sizeof(Vertex));
}

void SceneRenderer::replaceScene(std::unique_ptr<Scene> scene)
{
    m_dev->vkDeviceWaitIdle(m_window->device());
    releaseResources();
    m_baseVertex.clear();
    m_scene = std::move(scene);
    m_initialCamera.reset();
    initResources();
}

void SceneRenderer::addNodeDraws(const ModelInstance& inst, const Node& node, int32_t matrixBase)
{
    const Model& model = *inst.model;
    const AnimationSet& anims = model.animations();
    // Material, texcoord and texture animation come from the second slot when
    // one is set (ModelInstance.SetAnimation with slot 1), else the first.
    const bool second = inst.secondary.index >= 0 && inst.secondary.index < static_cast<int>(anims.count());
    const AnimationState& primary = inst.animation;
    const AnimationState& matState = second ? inst.secondary : primary;
    const AnimationState& tcState = second && inst.secondaryTexcoord ? inst.secondary : primary;
    auto valid = [&](const AnimationState& a) { return a.index >= 0 && a.index < static_cast<int>(anims.count()); };
    const MaterialAnimationGroup* materialGroup
        = valid(matState) && !anims.material[matState.index].empty() ? &anims.material[matState.index] : nullptr;
    const TexcoordAnimationGroup* texcoordGroup
        = valid(tcState) && !anims.texcoord[tcState.index].empty() ? &anims.texcoord[tcState.index] : nullptr;
    const TextureAnimationGroup* textureGroup
        = valid(matState) && !anims.texture[matState.index].empty() ? &anims.texture[matState.index] : nullptr;
    const int frame = matState.frame;
    const int texcoordFrame = tcState.frame;
    const int start = model.firstMesh(node);
    const uint32_t base = m_baseVertex[inst.model];
    for (int k = 0; k < node.meshCount; k++) {
        const int meshIndex = start + k;
        if (meshIndex < 0 || meshIndex >= static_cast<int>(model.meshes().size())) {
            continue;
        }
        const Mesh& mesh = model.meshes()[meshIndex];
        const Material& material = model.materials().at(mesh.materialId);
        const DrawRange& range = model.dlistRange(mesh.dlistId);
        if (range.vertexCount == 0) {
            continue;
        }
        DrawItem item{};
        item.model = inst.model;
        item.materialId = mesh.materialId;
        item.firstVertex = base + range.firstVertex;
        item.vertexCount = range.vertexCount;
        item.matrixBase = matrixBase;
        item.billboard = static_cast<int>(node.billboardMode);
        item.emission = inst.emission;
        item.lightIndex = m_lightIndex;
        item.diffuse = material.diffuse;
        item.ambient = material.ambient;
        item.specular = material.specular;
        item.alpha = material.alpha;
        const float instanceAlpha = inst.alpha;

        // Model.AnimateMaterials
        if (materialGroup != nullptr) {
            if (auto it = materialGroup->animations.find(material.name); it != materialGroup->animations.end()) {
                const MaterialAnimation& a = it->second;
                const int fc = materialGroup->frameCount;
                if (!(material.animationFlags & 0x1)) {
                    for (int c = 0; c < 3; c++) {
                        item.diffuse[c] = interpolate(materialGroup->colors, a.diffuse[c], frame, fc) / 31.0f;
                        item.ambient[c] = interpolate(materialGroup->colors, a.ambient[c], frame, fc) / 31.0f;
                        item.specular[c] = interpolate(materialGroup->colors, a.specular[c], frame, fc) / 31.0f;
                    }
                }
                if (!(material.animationFlags & 0x2)) {
                    item.alpha = interpolate(materialGroup->colors, a.alpha, frame, fc) / 31.0f;
                }
            }
        }

        for (const auto& [materialId, diffuse] : inst.diffuseOverrides) {
            if (materialId == mesh.materialId) {
                item.diffuse = diffuse;
            }
        }

        // Model.AnimateTextures
        int textureId = material.textureId;
        int paletteId = material.paletteId;
        if (textureGroup != nullptr) {
            if (auto it = textureGroup->animations.find(material.name); it != textureGroup->animations.end()) {
                const TextureAnimation& a = it->second;
                for (int j = a.startIndex; j < a.startIndex + a.count && j < static_cast<int>(textureGroup->frameIndices.size()); j++) {
                    if (textureGroup->frameIndices[j] == frame && j < static_cast<int>(textureGroup->textureIds.size())
                        && j < static_cast<int>(textureGroup->paletteIds.size())) {
                        textureId = textureGroup->textureIds[j];
                        paletteId = textureGroup->paletteIds[j];
                        break;
                    }
                }
            }
        }
        item.alpha *= instanceAlpha;
        item.textured = textureId >= 0 && textureId < static_cast<int>(model.textureCount());
        item.textureSet = textureSetFor(inst.model, inst.recolor, textureId, paletteId, material.xRepeat, material.yRepeat);

        // EntityBase.GetTexcoordMatrix (normal texgen keeps the material matrix; see room.vert)
        item.texMtx = Mat4::identity();
        const TexcoordAnimation* texcoordAnim = nullptr;
        if (texcoordGroup != nullptr) {
            if (auto it = texcoordGroup->animations.find(material.name); it != texcoordGroup->animations.end()) {
                texcoordAnim = &it->second;
                item.texMtx = animateTexcoords(*texcoordGroup, it->second, texcoordFrame);
            }
        }
        if (material.texgenMode != TexgenMode::None && texcoordAnim == nullptr) {
            Mat4 materialMatrix = Mat4::translation(material.scaleS * material.translateS, material.scaleT * material.translateT, 0.0f);
            materialMatrix = Mat4::scale(material.scaleS, material.scaleT, 1.0f) * materialMatrix;
            materialMatrix = Mat4::rotationZ(material.rotateZ) * materialMatrix;
            item.texMtx = materialMatrix;
        }

        if (material.renderMode == RenderMode::Decal) {
            m_decalItems.push_back(item);
        } else {
            m_opaqueItems.push_back(item);
        }
        if (material.renderMode == RenderMode::Translucent || item.alpha < 1.0f) {
            m_translucentItems.push_back(item);
        }
    }
}

void SceneRenderer::animateNodes(const Model& model, const NodeAnimationGroup* group, int frame, int index,
    bool useNodeTransform, const Mat4& parentTransform, float animationScale)
{
    // Model.AnimateNodes: a node's own animation is relative to its parent's,
    // which the recursion leaves un-multiplied by the entity transform until after.
    const auto& nodes = model.nodes();
    for (int i = index; i >= 0 && i < static_cast<int>(nodes.size());) {
        const Node& node = nodes[i];
        Mat4 transform = useNodeTransform ? node.transform : Mat4::identity();
        if (group != nullptr) {
            if (auto it = group->animations.find(node.name); it != group->animations.end()) {
                transform = animateNode(*group, it->second, animationScale, frame);
                if (node.parentIndex >= 0) {
                    transform *= m_nodeAnimation[node.parentIndex];
                }
            }
        }
        m_nodeAnimation[i] = transform;
        if (node.childIndex >= 0) {
            animateNodes(model, group, frame, node.childIndex, useNodeTransform, parentTransform, animationScale);
        }
        m_nodeAnimation[i] *= parentTransform;
        i = node.nextIndex;
    }
}

void SceneRenderer::collectDraws(double seconds)
{
    m_matrices.clear();
    m_opaqueItems.clear();
    m_decalItems.clear();
    m_translucentItems.clear();
    // EntityBase.UpdateAnimFrames: one animation frame every other 60 Hz tick.
    if (m_scene->externalAnimation) {
        seconds = m_scene->seconds;
    }
    const auto ticks = static_cast<long long>(seconds * 30.0);
    for (ModelInstance& inst : m_scene->instances) {
        const Model& model = *inst.model;
        if (!inst.visible) {
            continue;
        }
        if (!m_scene->externalAnimation) {
            if (inst.animationTicks > ticks) {
                inst.animationTicks = ticks;
            }
            for (int step = 0; inst.animationTicks < ticks; inst.animationTicks++) {
                if (step++ < 120) {
                    inst.animation.advance();
                    inst.secondary.advance();
                }
            }
        }
        const AnimationSet& anims = model.animations();
        const int animIndex = inst.animation.index;
        const NodeAnimationGroup* nodeGroup
            = animIndex >= 0 && animIndex < static_cast<int>(anims.count()) && !anims.node[animIndex].empty() ? &anims.node[animIndex] : nullptr;
        const Mat4 parent = inst.modelTransform(seconds);
        m_nodeAnimation.assign(model.nodes().size(), Mat4::identity());
        if (!inst.nodeOverrides.empty()) {
            for (size_t i = 0; i < m_nodeAnimation.size() && i < inst.nodeOverrides.size(); i++) {
                m_nodeAnimation[i] = inst.nodeOverrides[i];
            }
        } else if (!model.nodes().empty()) {
            // RoomEntity does not use node transforms; every other entity does.
            animateNodes(model, nodeGroup, inst.animation.frame, 0, !inst.room && inst.useNodeTransform, parent,
                inst.animationScale > 0 ? inst.animationScale : model.scale());
        }
        // Model.UpdateMatrixStack: billboards keep scale and position only.
        auto stackMatrix = [&](size_t nodeIndex) {
            const Node& node = model.nodes()[nodeIndex];
            return node.billboardMode == BillboardMode::None ? m_nodeAnimation[nodeIndex] : m_nodeAnimation[nodeIndex].clearRotation();
        };
        m_lightIndex = 0;
        if (inst.lights) {
            // Its lights as one more matrix: vector, color, vector, color by column.
            Mat4 lights;
            const std::array<float, 3>* columns[] = {&inst.lights->light1Vector, &inst.lights->light1Color,
                &inst.lights->light2Vector, &inst.lights->light2Color};
            for (int c = 0; c < 4; c++) {
                for (int r = 0; r < 3; r++) {
                    lights.m[c][r] = (*columns[c])[r];
                }
            }
            m_matrices.push_back(lights);
            m_lightIndex = static_cast<int32_t>(m_matrices.size());
        }
        const auto& stack = model.nodeMatrixIds();
        const auto stackBase = static_cast<int32_t>(m_matrices.size());
        for (int nodeIndex : stack) {
            m_matrices.push_back(nodeIndex >= 0 && nodeIndex < static_cast<int>(model.nodes().size()) ? stackMatrix(nodeIndex) : parent);
        }
        auto nodeBase = [&](size_t index) {
            if (!stack.empty()) {
                return stackBase;
            }
            const auto base = static_cast<int32_t>(m_matrices.size());
            m_matrices.push_back(stackMatrix(index));
            return base;
        };
        if (inst.room) {
            // RoomEntity: every enabled node.
            for (size_t i = 0; i < model.nodes().size(); i++) {
                if (model.nodes()[i].enabled) {
                    addNodeDraws(inst, model.nodes()[i], nodeBase(i));
                }
            }
            continue;
        }
        // EntityBase.GetDrawItems: node 0, then children of enabled nodes and all siblings.
        auto visit = [&](auto&& self, int index) -> void {
            while (index >= 0 && index < static_cast<int>(model.nodes().size())) {
                const Node& node = model.nodes()[index];
                if (node.enabled) {
                    addNodeDraws(inst, node, nodeBase(static_cast<size_t>(index)));
                    if (node.childIndex >= 0) {
                        self(self, node.childIndex);
                    }
                }
                index = node.nextIndex;
            }
        };
        if (!model.nodes().empty()) {
            visit(visit, 0);
        }
    }
    if (m_matrices.size() > kMaxMatrices) {
        qWarning("scene needs %zu matrices, drawing the first %u", m_matrices.size(), kMaxMatrices);
        m_matrices.resize(kMaxMatrices);
    }
}

void SceneRenderer::placeCameraInRoom()
{
    if (m_scene->instances.empty() || m_scene->instances[0].model == nullptr) {
        return; // nothing loaded (the launcher before the game files are set up)
    }
    const Model& room = *m_scene->instances[0].model;
    QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D hi = -lo;
    for (const Vertex& v : room.vertices()) {
        lo = QVector3D(std::min(lo.x(), v.pos[0]), std::min(lo.y(), v.pos[1]), std::min(lo.z(), v.pos[2]));
        hi = QVector3D(std::max(hi.x(), v.pos[0]), std::max(hi.y(), v.pos[1]), std::max(hi.z(), v.pos[2]));
    }
    if (lo.x() > hi.x()) {
        return;
    }
    lo *= room.scale();
    hi *= room.scale();
    const QVector3D center = (lo + hi) / 2.0f;
    m_camera.position = QVector3D(center.x(), lo.y() + (hi.y() - lo.y()) * 0.35f, center.z());
    m_camera.yaw = 0.0f;
    m_camera.pitch = -5.0f;
}

void SceneRenderer::releaseResources()
{
    VkDevice device = m_window->device();
    if (m_ui) {
        m_ui->releaseUi();
    }
    destroyUiImage();
    for (Buffer& b : m_uiVertexBuffers) {
        destroyBuffer(b);
    }
    if (m_uiPipeline) {
        m_dev->vkDestroyPipeline(device, m_uiPipeline, nullptr);
        m_uiPipeline = VK_NULL_HANDLE;
    }
    for (auto& row : m_pipelines) {
        for (VkPipeline& p : row) {
            if (p) {
                m_dev->vkDestroyPipeline(device, p, nullptr);
                p = VK_NULL_HANDLE;
            }
        }
    }
    if (m_pipelineCache) {
        m_dev->vkDestroyPipelineCache(device, m_pipelineCache, nullptr);
    }
    if (m_pipelineLayout) {
        m_dev->vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    }
    if (m_descriptorPool) {
        m_dev->vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    }
    if (m_frameLayout) {
        m_dev->vkDestroyDescriptorSetLayout(device, m_frameLayout, nullptr);
    }
    if (m_textureLayout) {
        m_dev->vkDestroyDescriptorSetLayout(device, m_textureLayout, nullptr);
    }
    for (auto& [key, sampler] : m_samplers) {
        m_dev->vkDestroySampler(device, sampler, nullptr);
    }
    auto destroyTexture = [&](Texture& t) {
        if (t.view) {
            m_dev->vkDestroyImageView(device, t.view, nullptr);
        }
        if (t.image) {
            m_dev->vkDestroyImage(device, t.image, nullptr);
        }
        if (t.memory) {
            m_dev->vkFreeMemory(device, t.memory, nullptr);
        }
        t = {};
    };
    for (auto& [key, tex] : m_textures) {
        destroyTexture(tex);
    }
    for (Texture& tex : m_hudTextures) {
        destroyTexture(tex);
    }
    m_hudTextures.clear();
    m_hudSets.clear();
    for (Buffer& b : m_hudVertexBuffers) {
        destroyBuffer(b);
    }
    for (Buffer& b : m_dynamicVertexBuffers) {
        destroyBuffer(b);
    }
    m_dynamicVertexCapacity = {};
    m_dynamicItems.clear();
    m_hudVertexCapacity = {};
    if (m_hudPipeline) {
        m_dev->vkDestroyPipeline(device, m_hudPipeline, nullptr);
        m_hudPipeline = VK_NULL_HANDLE;
    }
    if (m_hudLayout) {
        m_dev->vkDestroyPipelineLayout(device, m_hudLayout, nullptr);
        m_hudLayout = VK_NULL_HANDLE;
    }
    if (m_linearSampler) {
        m_dev->vkDestroySampler(device, m_linearSampler, nullptr);
        m_linearSampler = VK_NULL_HANDLE;
    }
    destroyTexture(m_whiteTexture);
    for (Buffer& b : m_uniformBuffers) {
        destroyBuffer(b);
    }
    for (Buffer& b : m_matrixBuffers) {
        destroyBuffer(b);
    }
    destroyBuffer(m_vertexBuffer);
    m_samplers.clear();
    m_textures.clear();
    m_textureSets.clear();
    m_opaqueItems.clear();
    m_decalItems.clear();
    m_translucentItems.clear();
    m_pipelineCache = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE;
    m_frameLayout = VK_NULL_HANDLE;
    m_textureLayout = VK_NULL_HANDLE;
}

void SceneRenderer::recordPass(VkCommandBuffer cb, Pass pass, const std::vector<DrawItem>& items)
{
    VkPipeline bound = VK_NULL_HANDLE;
    VkDescriptorSet boundSet = VK_NULL_HANDLE;
    for (const DrawItem& item : items) {
        const Material& material = item.model->materials()[item.materialId];
        VkPipeline pipeline = m_pipelines[pass][m_noCull ? 0 : static_cast<int>(material.culling) % 3];
        if (pipeline != bound) {
            m_dev->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            bound = pipeline;
        }
        if (item.textureSet != boundSet) {
            m_dev->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 1, 1, &item.textureSet, 0, nullptr);
            boundSet = item.textureSet;
        }
        DrawConstants pc{};
        std::memcpy(pc.texMtx, item.texMtx.data(), sizeof(pc.texMtx));
        for (int c = 0; c < 3; c++) {
            pc.diffuse[c] = item.diffuse[c];
            pc.ambient[c] = item.ambient[c];
            pc.specular[c] = item.specular[c];
        }
        pc.diffuse[3] = item.alpha;
        pc.ambient[3] = material.lighting ? 1.0f : 0.0f;
        pc.specular[3] = static_cast<float>(material.polygonMode);
        pc.flags[0] = (item.textured ? 1 : 0) | (item.lightIndex << 1);
        pc.flags[1] = static_cast<int32_t>(material.texgenMode) | (item.billboard << 8) | (static_cast<int32_t>(item.emission & 0x7FFF) << 16);
        pc.flags[2] = pass;
        pc.flags[3] = item.matrixBase;
        m_dev->vkCmdPushConstants(cb, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        m_dev->vkCmdDraw(cb, item.vertexCount, 1, item.firstVertex, 0);
    }
}

void SceneRenderer::createHudPipeline()
{
    VkDevice device = m_window->device();
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_textureLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &push;
    check(m_dev->vkCreatePipelineLayout(device, &plInfo, nullptr, &m_hudLayout), "HUD pipeline layout");

    VkShaderModule vert = loadShader(QStringLiteral(":/shaders/hud.vert.spv"));
    VkShaderModule frag = loadShader(QStringLiteral(":/shaders/hud.frag.spv"));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{0, sizeof(HudVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(HudVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(HudVertex, u)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(HudVertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = m_window->sampleCountFlagBits();
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = 0xF;
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = m_hudLayout;
    info.renderPass = m_window->defaultRenderPass();
    check(m_dev->vkCreateGraphicsPipelines(device, m_pipelineCache, 1, &info, nullptr, &m_hudPipeline), "HUD pipeline");
    // The screens: Qt Quick renders premultiplied alpha.
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    check(m_dev->vkCreateGraphicsPipelines(device, m_pipelineCache, 1, &info, nullptr, &m_uiPipeline), "UI pipeline");
    m_dev->vkDestroyShaderModule(device, vert, nullptr);
    m_dev->vkDestroyShaderModule(device, frag, nullptr);

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.25f;
    check(m_dev->vkCreateSampler(device, &sampler, nullptr, &m_linearSampler), "HUD sampler");
}

VkDescriptorSet SceneRenderer::hudSet(int texture, bool linear)
{
    const auto key = std::make_pair(texture, linear);
    if (auto it = m_hudSets.find(key); it != m_hudSets.end()) {
        return it->second;
    }
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = m_descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_textureLayout;
    VkDescriptorSet set;
    check(m_dev->vkAllocateDescriptorSets(m_window->device(), &alloc, &set), "vkAllocateDescriptorSets");
    VkDescriptorImageInfo imageInfo{linear ? m_linearSampler : samplerFor(RepeatMode::Clamp, RepeatMode::Clamp),
        m_hudTextures.at(texture).view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    m_dev->vkUpdateDescriptorSets(m_window->device(), 1, &write, 0, nullptr);
    m_hudSets[key] = set;
    return set;
}

// Scene::dynamicDraws: one matrix each after the node matrices, vertices in a
// per-frame buffer.
void SceneRenderer::prepareDynamic(int frame)
{
    m_dynamicItems.clear();
    const std::vector<Vertex>& vertices = m_scene->dynamicVertices;
    if (m_scene->dynamicDraws.empty() || vertices.empty()) {
        return;
    }
    for (const DynamicDraw& draw : m_scene->dynamicDraws) {
        if (draw.model == nullptr || draw.vertexCount == 0 || m_matrices.size() >= kMaxMatrices) {
            continue;
        }
        DynamicItem item{};
        item.textureSet = textureSetFor(draw.model, draw.recolor, draw.textureId, draw.paletteId, draw.xRepeat, draw.yRepeat);
        item.matrixIndex = static_cast<int32_t>(m_matrices.size());
        m_matrices.push_back(draw.billboard ? draw.transform.clearRotation() : draw.transform);
        item.billboard = draw.billboard;
        item.alpha = draw.alpha;
        item.firstVertex = draw.firstVertex;
        item.vertexCount = draw.vertexCount;
        m_dynamicItems.push_back(item);
    }
    Buffer& vb = m_dynamicVertexBuffers[frame];
    if (m_dynamicVertexCapacity[frame] < vertices.size()) {
        destroyBuffer(vb);
        const size_t capacity = std::max<size_t>(vertices.size() * 2, 1024);
        vb = createBuffer(capacity * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_window->hostVisibleMemoryIndex(), true);
        m_dynamicVertexCapacity[frame] = capacity;
    }
    std::memcpy(vb.mapped, vertices.data(), vertices.size() * sizeof(Vertex));
}

void SceneRenderer::recordDynamic(VkCommandBuffer cb, int frame, Pass pass)
{
    if (m_dynamicItems.empty()) {
        return;
    }
    VkDeviceSize offset = 0;
    m_dev->vkCmdBindVertexBuffers(cb, 0, 1, &m_dynamicVertexBuffers[frame].buffer, &offset);
    m_dev->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[pass][0]); // no culling
    for (const DynamicItem& item : m_dynamicItems) {
        m_dev->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 1, 1, &item.textureSet, 0, nullptr);
        DrawConstants pc{};
        std::memcpy(pc.texMtx, Mat4::identity().data(), sizeof(pc.texMtx));
        pc.diffuse[0] = pc.diffuse[1] = pc.diffuse[2] = 1.0f;
        pc.diffuse[3] = item.alpha;
        pc.ambient[3] = 0.0f;  // unlit
        pc.specular[3] = 0.0f; // modulate
        pc.flags[0] = 1;
        pc.flags[1] = item.billboard ? 1 << 8 : 0;
        pc.flags[2] = pass;
        pc.flags[3] = item.matrixIndex;
        m_dev->vkCmdPushConstants(cb, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        m_dev->vkCmdDraw(cb, item.vertexCount, 1, item.firstVertex, 0);
    }
    m_dev->vkCmdBindVertexBuffers(cb, 0, 1, &m_vertexBuffer.buffer, &offset);
}

// Before the render pass: build the overlay, upload the textures it added and
// its vertices.
void SceneRenderer::prepareOverlay(int frame, const QSize& size)
{
    m_overlayDraws = nullptr;
    if (!m_overlay) {
        return;
    }
    const HudDrawList& draws = m_overlay->overlay(size.width(), size.height());
    const std::vector<Image>& images = m_overlay->overlayTextures();
    while (m_hudTextures.size() < images.size()) {
        m_hudTextures.push_back(createTexture(images[m_hudTextures.size()]));
    }
    if (draws.vertices.empty()) {
        return;
    }
    Buffer& vb = m_hudVertexBuffers[frame];
    if (m_hudVertexCapacity[frame] < draws.vertices.size()) {
        destroyBuffer(vb);
        const size_t capacity = std::max<size_t>(draws.vertices.size() * 2, 4096);
        vb = createBuffer(capacity * sizeof(HudVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_window->hostVisibleMemoryIndex(), true);
        m_hudVertexCapacity[frame] = capacity;
    }
    std::memcpy(vb.mapped, draws.vertices.data(), draws.vertices.size() * sizeof(HudVertex));
    m_overlayDraws = &draws;
}

void SceneRenderer::recordOverlay(VkCommandBuffer cb, int frame, const QSize& size)
{
    if (!m_overlayDraws) {
        return;
    }
    m_dev->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hudPipeline);
    VkDeviceSize offset = 0;
    m_dev->vkCmdBindVertexBuffers(cb, 0, 1, &m_hudVertexBuffers[frame].buffer, &offset);
    const float viewport[2] = {static_cast<float>(size.width()), static_cast<float>(size.height())};
    m_dev->vkCmdPushConstants(cb, m_hudLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(viewport), viewport);
    for (const HudDrawList::Batch& batch : m_overlayDraws->batches) {
        if (batch.texture < 0 || batch.texture >= static_cast<int>(m_hudTextures.size()) || batch.vertexCount == 0) {
            continue;
        }
        VkDescriptorSet set = hudSet(batch.texture, batch.linear);
        m_dev->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hudLayout, 0, 1, &set, 0, nullptr);
        m_dev->vkCmdDraw(cb, batch.vertexCount, 1, batch.firstVertex, 0);
    }
}

void SceneRenderer::destroyUiImage()
{
    VkDevice device = m_window->device();
    if (m_uiTexture.view) {
        m_dev->vkDestroyImageView(device, m_uiTexture.view, nullptr);
    }
    if (m_uiTexture.image) {
        m_dev->vkDestroyImage(device, m_uiTexture.image, nullptr);
    }
    if (m_uiTexture.memory) {
        m_dev->vkFreeMemory(device, m_uiTexture.memory, nullptr);
    }
    m_uiTexture = {};
    if (m_uiSet) {
        m_dev->vkFreeDescriptorSets(device, m_descriptorPool, 1, &m_uiSet);
        m_uiSet = VK_NULL_HANDLE;
    }
    m_uiSize = {};
}

void SceneRenderer::ensureUiImage(const QSize& size)
{
    if (m_uiTexture.image && m_uiSize == size) {
        return;
    }
    // Nothing may still be reading the old one.
    m_dev->vkDeviceWaitIdle(m_window->device());
    destroyUiImage();
    VkDevice device = m_window->device();
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()), 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(m_dev->vkCreateImage(device, &info, nullptr, &m_uiTexture.image), "UI image");
    VkMemoryRequirements req;
    m_dev->vkGetImageMemoryRequirements(device, m_uiTexture.image, &req);
    uint32_t memoryIndex = m_window->deviceLocalMemoryIndex();
    if (!(req.memoryTypeBits & (1u << memoryIndex))) {
        VkPhysicalDeviceMemoryProperties props;
        m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &props);
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if (req.memoryTypeBits & (1u << i)) {
                memoryIndex = i;
                break;
            }
        }
    }
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryIndex;
    check(m_dev->vkAllocateMemory(device, &alloc, nullptr, &m_uiTexture.memory), "UI memory");
    check(m_dev->vkBindImageMemory(device, m_uiTexture.image, m_uiTexture.memory, 0), "UI bind");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_uiTexture.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(m_dev->vkCreateImageView(device, &view, nullptr, &m_uiTexture.view), "UI view");
    VkDescriptorSetAllocateInfo setAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setAlloc.descriptorPool = m_descriptorPool;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &m_textureLayout;
    check(m_dev->vkAllocateDescriptorSets(device, &setAlloc, &m_uiSet), "UI descriptor set");
    VkDescriptorImageInfo imageInfo{samplerFor(RepeatMode::Clamp, RepeatMode::Clamp), m_uiTexture.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_uiSet;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    m_dev->vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    m_uiSize = size;
}

void SceneRenderer::uiBarrier(VkCommandBuffer cb, bool toSampled)
{
    // The screens' image between Qt Quick's pass (colour attachment) and this one's (sampled).
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_uiTexture.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = toSampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = toSampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = toSampled ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = toSampled ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    m_dev->vkCmdPipelineBarrier(cb, toSampled ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        toSampled ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void SceneRenderer::startNextFrame()
{
    VkCommandBuffer cb = m_window->currentCommandBuffer();
    const QSize size = m_window->swapChainImageSize();
    const int frame = m_window->currentFrame();
    const qint64 nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

    if (m_baseVertex.size() != m_scene->models.size()) {
        uploadVertices();
    }
    collectDraws((nowNs - m_startNs) / 1e9);
    prepareDynamic(frame);
    std::memcpy(m_matrixBuffers[frame].mapped, m_matrices.data(), m_matrices.size() * sizeof(Mat4));

    static const RoomMetadata noRoom{};
    const RoomMetadata& room = m_scene->room != nullptr ? *m_scene->room : noRoom;
    FrameUniforms u{};
    QMatrix4x4 proj = m_window->clipCorrectionMatrix();
    proj.perspective(m_camera.fovY, size.width() / static_cast<float>(std::max(1, size.height())), kNearClip, kFarClip);
    const QMatrix4x4 view = m_camera.view();
    copyMatrix(u.proj, proj);
    copyMatrix(u.view, view);
    // Renderer.TransformCamera: the view's inverse rotation, and its yaw alone.
    QMatrix4x4 sphere = view.inverted();
    sphere.setColumn(3, QVector4D(0, 0, 0, 1));
    QMatrix4x4 cylinder;
    cylinder.rotate(m_camera.yaw, 0, 1, 0);
    copyMatrix(u.billboardSphere, sphere);
    copyMatrix(u.billboardCylinder, cylinder);
    const auto l1 = room.light1Color.toFloat();
    const auto l2 = room.light2Color.toFloat();
    std::copy(room.light1Vector.begin(), room.light1Vector.end(), u.light1Vec);
    std::copy(l1.begin(), l1.end(), u.light1Col);
    std::copy(room.light2Vector.begin(), room.light2Vector.end(), u.light2Vec);
    std::copy(l2.begin(), l2.end(), u.light2Col);
    const auto fog = room.fogColor.toFloat();
    std::copy(fog.begin(), fog.end(), u.fogColor);
    u.fogColor[3] = 1.0f;
    const int fogOffset = room.fogOffset & 0x7FFF;
    u.fogParams[0] = room.fogEnabled ? 1.0f : 0.0f;
    u.fogParams[1] = fogOffset / static_cast<float>(0x7FFF);
    u.fogParams[2] = (fogOffset + 32 * (0x400 >> room.fogSlope)) / static_cast<float>(0x7FFF);
    std::memcpy(m_uniformBuffers[frame].mapped, &u, sizeof(u));

    prepareOverlay(frame, size);
    m_uiShown = false;
    if (m_ui && size.width() > 0 && size.height() > 0) {
        ensureUiImage(size);
        m_uiShown = m_ui->renderUi(m_uiTexture.image, size);
        if (m_uiShown) {
            Buffer& vb = m_uiVertexBuffers[frame];
            if (!vb.buffer) {
                vb = createBuffer(6 * sizeof(HudVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_window->hostVisibleMemoryIndex(), true);
            }
            const float w = static_cast<float>(size.width()), h = static_cast<float>(size.height());
            const HudVertex quad[6] = {{0, 0, 0, 0, 0xFFFFFFFF}, {w, 0, 1, 0, 0xFFFFFFFF}, {0, h, 0, 1, 0xFFFFFFFF},
                {w, 0, 1, 0, 0xFFFFFFFF}, {w, h, 1, 1, 0xFFFFFFFF}, {0, h, 0, 1, 0xFFFFFFFF}};
            std::memcpy(vb.mapped, quad, sizeof quad);
            uiBarrier(cb, true);
        }
    }

    VkClearValue clears[2]{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = m_window->defaultRenderPass();
    rp.framebuffer = m_window->currentFramebuffer();
    rp.renderArea.extent = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height())};
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    m_dev->vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0, 0, static_cast<float>(size.width()), static_cast<float>(size.height()), 0, 1};
    m_dev->vkCmdSetViewport(cb, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, rp.renderArea.extent};
    m_dev->vkCmdSetScissor(cb, 0, 1, &scissor);
    VkDeviceSize offset = 0;
    m_dev->vkCmdBindVertexBuffers(cb, 0, 1, &m_vertexBuffer.buffer, &offset);
    m_dev->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_frameSets[frame], 0, nullptr);

    recordPass(cb, PassOpaque, m_opaqueItems);
    recordDynamic(cb, frame, PassOpaque);
    recordPass(cb, PassDecal, m_decalItems);
    recordPass(cb, PassTranslucent, m_translucentItems);
    recordDynamic(cb, frame, PassTranslucent);
    recordOverlay(cb, frame, size);
    if (m_uiShown) {
        m_dev->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiPipeline);
        VkDeviceSize uiOffset = 0;
        m_dev->vkCmdBindVertexBuffers(cb, 0, 1, &m_uiVertexBuffers[frame].buffer, &uiOffset);
        const float viewportSize[2] = {static_cast<float>(size.width()), static_cast<float>(size.height())};
        m_dev->vkCmdPushConstants(cb, m_hudLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(viewportSize), viewportSize);
        m_dev->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hudLayout, 0, 1, &m_uiSet, 0, nullptr);
        m_dev->vkCmdDraw(cb, 6, 1, 0, 0);
    }

    m_dev->vkCmdEndRenderPass(cb);
    if (m_uiShown) {
        uiBarrier(cb, false); // back to where Qt Quick left it, for its next pass
    }
    m_window->frameReady();
    m_frameCount++;
    if (m_frameCallback) {
        m_frameCallback(m_frameCount);
    }
    m_window->requestUpdate();
}

} // namespace fp
