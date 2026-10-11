#pragma once

#include "../Renderer.hpp"
#include "../NativeRuntime/OpenTK/GL.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Mods
{
    class ThumbnailCapture final : public RendererPlatform::WindowEvents
    {
    public:
        ThumbnailCapture(const ThumbnailCapture&) = delete;
        ThumbnailCapture& operator=(const ThumbnailCapture&) = delete;
        ThumbnailCapture(ThumbnailCapture&&) = delete;
        ThumbnailCapture& operator=(ThumbnailCapture&&) = delete;
        ~ThumbnailCapture();

        [[nodiscard]] MphRead::Scene& Scene() const;
        [[nodiscard]] bool Succeeded() const noexcept;

        // One picture of a room from a camera: where it stands, what it
        // looks at, how far it is rolled (degrees, a multiple of 90) and
        // the PNG it is written to.
        struct View
        {
            OpenTK::Mathematics::Vector3 Position{};
            OpenTK::Mathematics::Vector3 Target{};
            float Roll = 0.0F;
            std::string Path{};
            // Up for the camera; none for the world's.
            OpenTK::Mathematics::Vector3 Up{};
        };
        // Every view, in one session of the room.
        [[nodiscard]] static bool CaptureViews(
            const std::string& roomKey,
            const std::vector<View>& views,
            std::int32_t width,
            std::int32_t height);
        // A translated level's portals, as CaptureRoom does for the key
        // "ROOM#portals" (CustomRooms::PortalImagePath).
        [[nodiscard]] static std::vector<View> PortalViews(const std::string& roomKey);

        [[nodiscard]] static std::int32_t CaptureRooms(
            const std::vector<std::string>& rooms,
            std::int32_t width,
            std::int32_t height);
        [[nodiscard]] static bool CaptureRoom(
            const std::string& roomKey,
            std::int32_t width,
            std::int32_t height);
        [[nodiscard]] static RendererPlatform::WindowSettings WindowSettings(
            std::int32_t width,
            std::int32_t height);

    private:
        static constexpr std::int32_t SettleFrames = 12;
        static constexpr std::int32_t RetryFrames = 20;
        static constexpr std::int32_t MaxAttempts = 3;

        [[nodiscard]] static RendererPlatform::WindowSettings GameSettings();

        ThumbnailCapture(
            const std::string& roomKey,
            std::int32_t width,
            std::int32_t height,
            std::vector<View> views = {});

        void OnLoad() override;
        void ApplyPreviewCamera();
        void OnRenderFrame(const RendererPlatform::FrameEventArgs& args) override;
        void OnClosing() override;
        void Run();
        void Close();

        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientSize() const;
        [[nodiscard]] bool IsVisible() const noexcept;
        void IsVisible(bool value);
        void Present();

        static bool _describedContext;

        std::shared_ptr<RendererPlatform::Window> _window{};
        std::unique_ptr<MphRead::NativeRuntime::Rhi::Swapchain> _swapchain{};
        std::shared_ptr<MphRead::Scene> _scene{};
        std::string _roomKey{};
        std::vector<View> _views{};
        std::size_t _view = 0;
        std::int32_t _settleFrames = 0;
        bool _captured = false;
        std::int32_t _attempts = 0;
        OpenTK::Graphics::OpenGL::ErrorCode _frameError
            = OpenTK::Graphics::OpenGL::ErrorCode::NoError;
        OpenTK::Mathematics::Vector2i _asked{};
        bool _isVisible = false;
    };
}
