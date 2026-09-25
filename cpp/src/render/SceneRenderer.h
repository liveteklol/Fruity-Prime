#pragma once

#include "Overlay.h"
#include "Scene.h"
#include "VulkanWindow.h"

#include <QMatrix4x4>
#include <QVector3D>

#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace fp {

struct Camera {
    QVector3D position;
    float yaw = 0.0f;   // degrees, 0 looks down -Z
    float pitch = 0.0f; // degrees
    float fovY = 78.0f;
    // When set, the view looks at `target` with `up` (first person), ignoring yaw/pitch.
    bool useTarget = false;
    QVector3D target;
    QVector3D up{0, 1, 0};

    QVector3D forward() const;
    QVector3D right() const;
    QMatrix4x4 view() const;
};

// A layer drawn over everything else (the QML screens), rendered by its owner
// into an image this renderer provides: RGBA8, premultiplied alpha, left in
// COLOR_ATTACHMENT_OPTIMAL between frames.
class UiLayer {
public:
    virtual ~UiLayer() = default;
    // Before the frame is recorded: brings `image` up to date (only when
    // something changed). False when there is nothing to show this frame.
    virtual bool renderUi(VkImage image, const QSize& size) = 0;
    // The image and the device are about to go.
    virtual void releaseUi() = 0;
};

class SceneRenderer : public VulkanRenderer {
public:
    SceneRenderer(VulkanWindow* window, std::unique_ptr<Scene> scene);

    void initResources() override;
    void releaseResources() override;
    void startNextFrame() override;

    Camera& camera() { return m_camera; }
    Scene& scene() { return *m_scene; }
    void setNoCull(bool noCull) { m_noCull = noCull; }
    void setInitialCamera(std::optional<Camera> camera) { m_initialCamera = camera; }
    // Called after each frame is submitted, with the frame count so far.
    void setFrameCallback(std::function<void(int)> callback) { m_frameCallback = std::move(callback); }
    // 2D drawn over the scene every frame (the HUD).
    void setOverlay(OverlaySource* overlay) { m_overlay = overlay; }
    // The screens over the HUD.
    void setUiLayer(UiLayer* layer) { m_ui = layer; }
    // Another room: everything uploaded for the old scene goes, the new one's is uploaded.
    void replaceScene(std::unique_ptr<Scene> scene);

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    enum Pass { PassOpaque = 0, PassDecal = 1, PassTranslucent = 2, PassCount = 3 };
    struct DrawItem {
        const Model* model;
        int materialId;
        uint32_t firstVertex;
        uint32_t vertexCount;
        VkDescriptorSet textureSet;
        int32_t matrixBase;
        int billboard;
        bool textured;
        uint16_t emission; // BGR555
        int32_t lightIndex; // 1 + the matrix holding the instance's own lights, 0 for the room's
        std::array<float, 3> diffuse, ambient, specular; // after material animation
        float alpha;
        Mat4 texMtx;
    };

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryIndex, bool map);
    void destroyBuffer(Buffer& buffer);
    Texture createTexture(const Image& image);
    VkShaderModule loadShader(const QString& resource);
    void createPipelines();
    // Every model's vertices in one buffer, again when models were loaded after it was built.
    void uploadVertices();
    VkSampler samplerFor(RepeatMode x, RepeatMode y);
    VkDescriptorSet textureSetFor(const Model* model, int recolor, int textureId, int paletteId, RepeatMode x, RepeatMode y);
    void collectDraws(double seconds);
    void addNodeDraws(const ModelInstance& inst, const Node& node, int32_t matrixBase);
    int32_t m_lightIndex = 0; // the instance being collected's DrawItem::lightIndex
    void animateNodes(const Model& model, const NodeAnimationGroup* group, int frame, int index, bool useNodeTransform,
        const Mat4& parentTransform, float animationScale);
    void recordPass(VkCommandBuffer cb, Pass pass, const std::vector<DrawItem>& items);
    void placeCameraInRoom();
    void createHudPipeline();
    VkDescriptorSet hudSet(int texture, bool linear);
    void prepareDynamic(int frame);
    void recordDynamic(VkCommandBuffer cb, int frame, Pass pass);
    void prepareOverlay(int frame, const QSize& size);
    void recordOverlay(VkCommandBuffer cb, int frame, const QSize& size);

    VulkanWindow* m_window;
    QVulkanDeviceFunctions* m_dev = nullptr;
    std::unique_ptr<Scene> m_scene;
    Camera m_camera;
    std::optional<Camera> m_initialCamera;
    std::function<void(int)> m_frameCallback;
    int m_frameCount = 0;
    bool m_noCull = false;
    qint64 m_startNs = 0;

    Buffer m_vertexBuffer;
    std::map<const Model*, uint32_t> m_baseVertex;
    static constexpr uint32_t kMaxMatrices = 16384;
    std::array<Buffer, VulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_uniformBuffers{};
    std::array<Buffer, VulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_matrixBuffers{};
    std::array<VkDescriptorSet, VulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_frameSets{};
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_frameLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    VkPipeline m_pipelines[PassCount][3] = {}; // [pass][culling mode]
    std::map<int, VkSampler> m_samplers;
    std::map<std::tuple<const Model*, int, int, int>, Texture> m_textures;
    std::map<std::tuple<const Model*, int, int, int, int>, VkDescriptorSet> m_textureSets;
    Texture m_whiteTexture;

    OverlaySource* m_overlay = nullptr;
    const HudDrawList* m_overlayDraws = nullptr;
    VkPipelineLayout m_hudLayout = VK_NULL_HANDLE;
    VkPipeline m_hudPipeline = VK_NULL_HANDLE;
    VkPipeline m_uiPipeline = VK_NULL_HANDLE; // premultiplied alpha
    UiLayer* m_ui = nullptr;
    Texture m_uiTexture;
    QSize m_uiSize;
    VkDescriptorSet m_uiSet = VK_NULL_HANDLE;
    bool m_uiShown = false;
    std::array<Buffer, VulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_uiVertexBuffers{};
    void ensureUiImage(const QSize& size);
    void destroyUiImage();
    void uiBarrier(VkCommandBuffer cb, bool toSampled);
    VkSampler m_linearSampler = VK_NULL_HANDLE;
    std::vector<Texture> m_hudTextures;
    std::map<std::pair<int, bool>, VkDescriptorSet> m_hudSets;
    std::array<Buffer, VulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_hudVertexBuffers{};
    std::array<size_t, VulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_hudVertexCapacity{};

    // Scene::dynamicDraws, uploaded each frame.
    struct DynamicItem {
        VkDescriptorSet textureSet;
        int32_t matrixIndex;
        bool billboard;
        float alpha;
        uint32_t firstVertex, vertexCount;
    };
    std::vector<DynamicItem> m_dynamicItems;
    std::array<Buffer, VulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_dynamicVertexBuffers{};
    std::array<size_t, VulkanWindow::MAX_CONCURRENT_FRAME_COUNT> m_dynamicVertexCapacity{};

    // Rebuilt every frame.
    std::vector<Mat4> m_matrices;
    std::vector<Mat4> m_nodeAnimation;
    std::vector<DrawItem> m_opaqueItems;
    std::vector<DrawItem> m_decalItems;
    std::vector<DrawItem> m_translucentItems;
};

} // namespace fp
