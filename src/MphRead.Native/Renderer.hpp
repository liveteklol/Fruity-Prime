#pragma once

#include "Formats/Enums.hpp"
#include "Formats/Types.hpp"
#include "Metadata/Metadata.hpp"
#include "Selection.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"
#include "NativeRuntime/Rhi/GraphicsDevice.hpp"
#include "NativeRuntime/Rhi/SceneShaders.hpp"
#include "NativeRuntime/System/Buffers.hpp"
#include "RendererGpuMesh.hpp"
#include "NativeRuntime/System/Runtime.hpp"

namespace MphRead::Mods::Diagnostics { class FramePerformance; }

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <version>
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#include <stop_token>
#define MPHREAD_HAS_STD_JTHREAD 1
#else
#define MPHREAD_HAS_STD_JTHREAD 0
#endif
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace OpenTK::Mathematics
{
    struct Vector2i final
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;

        constexpr Vector2i() noexcept = default;
        constexpr Vector2i(std::int32_t x, std::int32_t y) noexcept : X(x), Y(y) {}

        friend constexpr bool operator==(Vector2i left, Vector2i right) noexcept
        {
            return left.X == right.X && left.Y == right.Y;
        }
        friend constexpr bool operator!=(Vector2i left, Vector2i right) noexcept
        {
            return !(left == right);
        }
    };

}

namespace MphRead::RendererDetail
{
    using ::OpenTK::Mathematics::IdentityMatrix;
}

namespace MphRead
{
#if MPHREAD_HAS_STD_JTHREAD
    using RendererStopToken = std::stop_token;
    using RendererJThread = std::jthread;
#else
    namespace RendererDetail
    {
        class OutputStopState final
        {
        public:
            [[nodiscard]] bool RequestStop() noexcept
            {
                bool changed = false;
                {
                    std::scoped_lock lock(_mutex);
                    if (!_stopRequested)
                    {
                        _stopRequested = true;
                        changed = true;
                    }
                }
                if (changed)
                {
                    _condition.notify_all();
                }
                return changed;
            }

            [[nodiscard]] bool StopRequested() const noexcept
            {
                std::scoped_lock lock(_mutex);
                return _stopRequested;
            }

            void WaitFor(std::chrono::milliseconds duration)
            {
                std::unique_lock lock(_mutex);
                _condition.wait_for(lock, duration, [this] { return _stopRequested; });
            }

        private:
            mutable std::mutex _mutex{};
            std::condition_variable _condition{};
            bool _stopRequested = false;
        };
    }

    class RendererStopToken final
    {
    public:
        RendererStopToken() noexcept = default;

        [[nodiscard]] bool stop_requested() const noexcept
        {
            return _state && _state->StopRequested();
        }

        void WaitFor(std::chrono::milliseconds duration) const
        {
            if (_state)
            {
                _state->WaitFor(duration);
            }
            else
            {
                std::this_thread::sleep_for(duration);
            }
        }

    private:
        friend class RendererJThread;
        explicit RendererStopToken(std::shared_ptr<RendererDetail::OutputStopState> state) noexcept
            : _state(std::move(state))
        {
        }

        std::shared_ptr<RendererDetail::OutputStopState> _state{};
    };

    class RendererJThread final
    {
    public:
        RendererJThread() noexcept = default;

        explicit RendererJThread(std::function<void(RendererStopToken)> entry)
            : _state(std::make_shared<RendererDetail::OutputStopState>()),
              _thread([state = _state, entry = std::move(entry)]() mutable
              {
                  entry(RendererStopToken(std::move(state)));
              })
        {
        }

        RendererJThread(const RendererJThread&) = delete;
        RendererJThread& operator=(const RendererJThread&) = delete;

        RendererJThread(RendererJThread&& other) noexcept
            : _state(std::move(other._state)),
              _thread(std::move(other._thread))
        {
        }

        RendererJThread& operator=(RendererJThread&& other) noexcept
        {
            if (this != &other)
            {
                StopAndJoin();
                _state = std::move(other._state);
                _thread = std::move(other._thread);
            }
            return *this;
        }

        ~RendererJThread()
        {
            StopAndJoin();
        }

        [[nodiscard]] bool joinable() const noexcept
        {
            return _thread.joinable();
        }

        bool request_stop() noexcept
        {
            return _state && _state->RequestStop();
        }

    private:
        void StopAndJoin()
        {
            if (_thread.joinable())
            {
                request_stop();
                _thread.join();
            }
        }

        std::shared_ptr<RendererDetail::OutputStopState> _state{};
        std::thread _thread{};
    };
#endif

    inline void RendererWaitForStop(
        std::condition_variable_any& condition,
        std::unique_lock<std::mutex>& lock,
        const RendererStopToken& token,
        std::chrono::milliseconds duration)
    {
#if MPHREAD_HAS_STD_JTHREAD
        condition.wait_for(lock, token, duration, [] { return false; });
#else
        (void)condition;
        (void)lock;
        token.WaitFor(duration);
#endif
    }

#undef MPHREAD_HAS_STD_JTHREAD

    class Scene;

    class Model;
    // Model textures are reconstructed from the original asset data, without
    // retaining a second expanded pixel image while playing. The original
    // model must live as long as its scene-owned textures, even when the
    // uploading entity or mesh cache has already released it.
    struct SceneModelTextureSource
    {
        std::shared_ptr<MphRead::Model> Model{};
        std::int32_t TextureId = 0;
        std::int32_t PaletteId = 0;
        std::int32_t RecolorId = 0;
    };
    // HUD/dynamic uploads whose caller supplies temporary pixels still need
    // recovery data. This is not a CPU rendering path or GPU readback.
    struct SceneTextureCopy
    {
        std::int32_t Width = 0;
        std::int32_t Height = 0;
        NativeRuntime::Rhi::TextureFormat Format{};
        bool Owned = false;
        std::vector<std::uint8_t> Pixels{};
    };
    class RenderWindow;
    class TextureMap;
    enum class Movie : std::int32_t;
    enum class AfterMovie : std::int32_t;
    namespace Formats::Culling { struct NodeRef; struct FrustumPlane; class FrustumInfo; }
    class RoomMetadata;
    class Model;
    class ModelInstance;
    class Material;
    class Mesh;
    namespace Formats { struct CollisionResult; }
    namespace Formats::Collision { class EntityCollision; }
    namespace NativeRuntime::Rhi { class ShaderConstantSink; }
    namespace Hud { class LayerInfo; class HudObjectInstance; }
    class Effect;
    class EffectElement;
    namespace Effects
    {
        class EffectEntry;
        class EffectElementEntry;
        class EffectParticle;
        class SingleParticle;
    }
    class LightInfo;
    class Node;
    namespace NativeRuntime::Rhi { class Swapchain; enum class SceneBackendRequest : std::uint8_t; }

    namespace Entities
    {
        class EntityBase;
        class PlayerEntity;
        class RoomEntity;
        struct BeamEffectEntityData;
        class BeamEffectEntity;
        class BombEntity;
        class PlatformEntity;
        class EnemyInstanceEntity;
        class PointModuleEntity;
    }

    enum class VolumeDisplay : std::int32_t
    {
        None,
        LightColor1,
        LightColor2,
        TriggerParent,
        TriggerChild,
        AreaInside,
        AreaExit,
        MorphCamera,
        JumpPad,
        Teleporter,
        EnemyHurt,
        Object,
        FlagBase,
        DefenseNode,
        KillPlane,
        PlayerLimit,
        CameraLimit,
        NodeBounds,
        NodeData,
        Portal
    };

    enum class CollisionType : std::int32_t
    {
        Any,
        Player,
        Beam,
        Both
    };

    enum class CollisionColor : std::int32_t
    {
        None,
        Entity,
        Terrain,
        Type
    };

    enum class CameraMode : std::int32_t
    {
        Pivot,
        Roam,
        Player
    };

    enum class AfterFade : std::int32_t
    {
        None,
        Exit,
        LoadRoom,
        PlayMovie,
        StopMovie,
        EnterShip
    };

    namespace RendererPlatform
    {
        using Key = ::OpenTK::Windowing::GraphicsLibraryFramework::Keys;
        using MouseButton = ::OpenTK::Windowing::GraphicsLibraryFramework::MouseButton;
        using KeyboardState = ::OpenTK::Windowing::GraphicsLibraryFramework::KeyboardState;
        using MouseState = ::OpenTK::Windowing::GraphicsLibraryFramework::MouseState;
        using KeyboardKeyEventArgs = ::OpenTK::Windowing::Common::KeyboardKeyEventArgs;

        // PlayerInput.hpp owns the canonical native OpenTK key enum but its current
        // source slice does not yet name every OpenTK key Renderer.cs observes.
        // These are the exact OpenTK/GLFW numeric key values, kept as constants
        // rather than extending or duplicating that provider-owned enum.
        inline constexpr Key PeriodKey = static_cast<Key>(46);
        inline constexpr Key LeftControlKey = static_cast<Key>(341);
        inline constexpr Key LeftAltKey = static_cast<Key>(342);
        inline constexpr Key RightShiftKey = static_cast<Key>(344);
        inline constexpr Key RightControlKey = static_cast<Key>(345);
        inline constexpr Key RightAltKey = static_cast<Key>(346);
        inline constexpr Key EscapeKey = static_cast<Key>(256);
        inline constexpr Key F1Key = static_cast<Key>(290);
        inline constexpr Key F2Key = static_cast<Key>(291);
        enum class CursorState : std::int32_t { Normal, Grabbed };
        enum class GraphicsWindowMode : std::uint8_t { OpenGL, NoApi };

        class GLFWException final : public std::runtime_error
        {
        public:
            GLFWException(std::string description, std::int32_t errorCode)
                : std::runtime_error(std::move(description)), _errorCode(errorCode)
            {
            }

            [[nodiscard]] std::int32_t ErrorCode() const noexcept { return _errorCode; }

        private:
            std::int32_t _errorCode;
        };

        struct FrameEventArgs final { double Time = 0.0; };
        struct ResizeEventArgs final { OpenTK::Mathematics::Vector2i Size{}; };
        struct WindowPositionEventArgs final { OpenTK::Mathematics::Vector2i Position{}; };
        struct MouseButtonEventArgs final { MouseButton Button{}; };
        struct MouseMoveEventArgs final
        {
            float X = 0.0F;
            float Y = 0.0F;
            float DeltaX = 0.0F;
            float DeltaY = 0.0F;
        };
        struct MouseWheelEventArgs final { float OffsetX = 0.0F; float OffsetY = 0.0F; };
        struct TextInputEventArgs final { std::uint32_t Unicode = 0; };

        struct WindowSettings final
        {
            OpenTK::Mathematics::Vector2i ClientSize{1280, 768};
            std::string Title{};
            double UpdateFrequency = 0.0;
            bool StartVisible = false;
            enum class ContextProfile : std::int32_t { Any, Compatability };
            enum class ContextFlags : std::int32_t { Default };
            ContextProfile Profile = ContextProfile::Compatability;
            ContextFlags Flags = ContextFlags::Default;
            std::int32_t ApiMajor = 3;
            std::int32_t ApiMinor = 2;
            GraphicsWindowMode GraphicsMode = GraphicsWindowMode::OpenGL;
        };

        struct WindowIconImage final
        {
            std::int32_t Width = 0;
            std::int32_t Height = 0;
            std::vector<std::uint8_t> Pixels{};
        };

        struct WindowIcon final
        {
            std::vector<WindowIconImage> Images{};
        };

        // The overridable members of OpenTK's GameWindow, which GameWindow.Run
        // calls from its loop. The base members only raise events that nothing
        // here subscribes to, so their defaults do nothing.
        class WindowEvents
        {
        public:
            virtual ~WindowEvents() = default;
            virtual void OnLoad() {}
            [[nodiscard]] virtual bool BeforeFrame() { return true; }
            virtual bool CanSampleInputWhileWaiting() const { return true; }
            virtual void OnInputSample() {}
            virtual void OnInputEventsProcessed() {}
            virtual void OnRenderFrame(const FrameEventArgs& args) { (void)args; }
            virtual void OnResize(const ResizeEventArgs& e) { (void)e; }
            virtual void OnMove(const WindowPositionEventArgs& e) { (void)e; }
            virtual void OnMaximizedChanged(bool maximized) { (void)maximized; }
            virtual void OnFocusedChanged(bool focused) { (void)focused; }
            virtual void OnMouseDown(const MouseButtonEventArgs& e) { (void)e; }
            virtual void OnMouseUp(const MouseButtonEventArgs& e) { (void)e; }
            virtual void OnMouseMove(const MouseMoveEventArgs& e) { (void)e; }
            virtual void OnMouseWheel(const MouseWheelEventArgs& e) { (void)e; }
            virtual void OnTextInput(const TextInputEventArgs& e) { (void)e; }
            virtual void OnKeyDown(const KeyboardKeyEventArgs& e) { (void)e; }
            virtual void OnKeyUp(const KeyboardKeyEventArgs& e) { (void)e; }
            virtual void OnClosing() {}
        };

        struct MonitorArea final
        {
            OpenTK::Mathematics::Vector2i Min{};
            OpenTK::Mathematics::Vector2i Size{};
        };

        // OpenTK.Windowing.Common.WindowBorder.
        enum class WindowBorderValue : std::int32_t { Resizable = 0, Fixed = 1, Hidden = 2 };

        // OpenTK.Windowing.Common.WindowState, as far as the game reads it.
        enum class WindowStateValue : std::int32_t
        {
            Normal = 0,
            Minimized = 1,
            Maximized = 2,
            Fullscreen = 3
        };

        class Window
        {
        public:
            virtual ~Window() = default;
            // GameWindow.Run: OnLoad, then the event and frame loop until the
            // window closes, delivering each callback to events.
            virtual void Run(WindowEvents& events) = 0;
            [[nodiscard]] virtual OpenTK::Mathematics::Vector2i Size() const = 0;
            [[nodiscard]] virtual KeyboardState& Keyboard() = 0;
            [[nodiscard]] virtual MouseState& Mouse() = 0;
            virtual void Title(std::string value) = 0;
            virtual void MinimumSize(OpenTK::Mathematics::Vector2i value) = 0;
            virtual void Cursor(RendererPlatform::CursorState value) = 0;
            virtual void UpdateFrequency(double value) = 0;
            virtual void PresentationTiming(NativeRuntime::Rhi::PresentMode, std::int32_t,
                NativeRuntime::Rhi::PacingAuthority) {}
            virtual void PresentationAccepted() {}
            virtual void PresentationUnavailable() {}
            virtual void Visible(bool value) = 0;
            virtual void SetIcon(const WindowIcon& icon) = 0;
            [[nodiscard]] virtual void* NativeHandle() const = 0;
            [[nodiscard]] virtual GraphicsWindowMode GraphicsMode() const noexcept = 0;
            virtual void Close() = 0;
            virtual void BaseOnClosing() = 0;
            virtual void BaseOnLoad() = 0;
            virtual void BaseOnRenderFrame(const FrameEventArgs& args) = 0;
            virtual void BaseOnResize(const ResizeEventArgs& e) = 0;
            virtual void BaseOnMove(const WindowPositionEventArgs& e) = 0;
            virtual void BaseOnMaximizedChanged(bool maximized) = 0;
            virtual void BaseOnFocusedChanged(bool focused) = 0;
            virtual void BaseOnMouseDown(const MouseButtonEventArgs& e) = 0;
            virtual void BaseOnMouseUp(const MouseButtonEventArgs& e) = 0;
            virtual void BaseOnMouseMove(const MouseMoveEventArgs& e) = 0;
            virtual void BaseOnMouseWheel(const MouseWheelEventArgs& e) = 0;
            virtual void BaseOnTextInput(const TextInputEventArgs& e) = 0;
            virtual void BaseOnKeyDown(const KeyboardKeyEventArgs& e) = 0;
            virtual void BaseOnKeyUp(const KeyboardKeyEventArgs& e) = 0;

            // NativeWindow's own properties, as WindowMode reads and sets them.
            [[nodiscard]] virtual std::int32_t WindowBorder() const = 0;
            virtual void WindowBorder(std::int32_t value) = 0;
            [[nodiscard]] virtual OpenTK::Mathematics::Vector2i Location() const = 0;
            virtual void Location(OpenTK::Mathematics::Vector2i value) = 0;
            [[nodiscard]] virtual OpenTK::Mathematics::Vector2i ClientSize() const = 0;
            virtual void ClientSize(OpenTK::Mathematics::Vector2i value) = 0;
            // CurrentMonitor.ClientArea.
            [[nodiscard]] virtual MonitorArea CurrentMonitorClientArea() const = 0;
            // CurrentMonitor.WorkArea, kept separate for FitToScreen.
            [[nodiscard]] virtual MonitorArea CurrentMonitorWorkArea() const = 0;
            // Monitors.GetMonitors(), each one's ClientArea.
            [[nodiscard]] virtual std::vector<MonitorArea> MonitorClientAreas() const = 0;
            [[nodiscard]] virtual WindowStateValue WindowState() const = 0;
            virtual void WindowStateMinimized() = 0;
            virtual void WindowStateMaximized() = 0;
            virtual void WindowStateNormal() = 0;
            // The toolkit's own fullscreen state: the window *is* the
            // monitor, at its exact size, which is what lets the driver
            // give the swapchain the display (exclusive / independent flip).
            // A window that cannot do it answers false and WindowMode
            // falls back to borderless.
            [[nodiscard]] virtual bool WindowStateFullscreen() { return false; }
            // Borderless fullscreen: no frame, covering the window's monitor
            // one pixel row short of its height (so the driver never treats
            // it as exclusive), sized in the toolkit's own units. A window
            // that answers false is placed by WindowMode from
            // CurrentMonitorClientArea instead -- which is in physical
            // pixels, while a Qt window's ClientSize is in logical ones, and
            // at 150 % scaling that made a window 1.5x the monitor showing
            // only its top-left corner.
            [[nodiscard]] virtual bool WindowStateBorderless() { return false; }
            // The refresh rate of the screen the window is on, in Hz, or 0
            // when the toolkit does not say. Diagnostics only.
            [[nodiscard]] virtual double RefreshRate() const { return 0.0; }
            virtual void Floating(bool value) = 0;
            [[nodiscard]] virtual bool IsFocused() const = 0;
            // NativeWindow.ClientLocation: the client area's screen origin.
            [[nodiscard]] virtual OpenTK::Mathematics::Vector2i ClientLocation() const = 0;
            virtual void Focus() = 0;
        };

        // NativeWindow.ProcessEvents(0): the pending window messages, drained
        // without waiting.
        void ProcessEvents();
        // The OpenGL context current on this thread, as the window toolkit
        // names it (a GLFWwindow, a QOpenGLContext), or null; and making one
        // of those current again. The OpenGL RHI keys its sessions on these.
        [[nodiscard]] void* CurrentGlContext() noexcept;
        void MakeGlContextCurrent(void* context) noexcept;

        [[nodiscard]] std::shared_ptr<Window> CreateWindow(const WindowSettings& settings);
        [[nodiscard]] OpenTK::Mathematics::Vector2i WorkAreaForWindow(Window& window);
        void InstallGlfwErrorCallback(std::function<void(std::int32_t, std::string)> callback);
        [[nodiscard]] std::int32_t GlfwFeatureUnavailableCode();
    }

    template <typename T>
    class RendererConcurrentQueue final
    {
    public:
        RendererConcurrentQueue() = default;
        RendererConcurrentQueue(const RendererConcurrentQueue&) = delete;
        RendererConcurrentQueue& operator=(const RendererConcurrentQueue&) = delete;

        void Enqueue(T value)
        {
            std::scoped_lock lock(_mutex);
            _items.push_back(std::move(value));
        }

        [[nodiscard]] bool TryDequeue(T& value)
        {
            std::scoped_lock lock(_mutex);
            if (_items.empty())
            {
                return false;
            }
            value = std::move(_items.front());
            _items.pop_front();
            return true;
        }

        [[nodiscard]] std::int32_t Count() const
        {
            std::scoped_lock lock(_mutex);
            return static_cast<std::int32_t>(_items.size());
        }

    private:
        mutable std::mutex _mutex{};
        std::deque<T> _items{};
    };

    struct TextureMapValue final
    {
        std::int32_t BindingId = 0;
        bool OnlyOpaque = false;
    };

    class TextureMap
    {
    public:
        TextureMap() = default;
        TextureMap(const TextureMap&) = delete;
        TextureMap& operator=(const TextureMap&) = delete;
        TextureMap(TextureMap&&) = delete;
        TextureMap& operator=(TextureMap&&) = delete;
        virtual ~TextureMap() = default;

        [[nodiscard]] TextureMapValue Get(std::int32_t textureId, std::int32_t paletteId,
            std::int32_t recolorId) const;
        void Add(std::int32_t textureId, std::int32_t paletteId, std::int32_t recolorId,
            std::int32_t bindingId, bool onlyOpaque);

    private:
        friend class Scene;
        using Entry = std::pair<std::int32_t, TextureMapValue>;

        [[nodiscard]] std::int32_t GetKey(std::int32_t textureId,
            std::int32_t paletteId, std::int32_t recolorId) const;
        [[nodiscard]] std::optional<std::size_t> FindIndex(std::int32_t key) const noexcept;
        [[nodiscard]] TextureMapValue GetItem(std::int32_t key) const;
        void SetItem(std::int32_t key, TextureMapValue value);

        std::vector<Entry> _items{};
    };

    class RenderWindow : public RendererPlatform::WindowEvents
    {
    public:
        static void LogCreatingWindow();
        explicit RenderWindow(bool shell = false);
        RenderWindow(const RenderWindow&) = delete;
        RenderWindow& operator=(const RenderWindow&) = delete;
        RenderWindow(RenderWindow&&) = delete;
        RenderWindow& operator=(RenderWindow&&) = delete;
        ~RenderWindow() override;

        [[nodiscard]] bool HasScene() const noexcept;
        [[nodiscard]] MphRead::Scene& Scene() const;
        [[nodiscard]] OpenTK::Mathematics::Vector2i FramebufferSize() const;
        [[nodiscard]] void* WindowPtr() const;
        void Title(std::string value);
        [[nodiscard]] std::shared_ptr<MphRead::Scene> NewSideScene();
        MphRead::Scene& BeginScene();
        void LoadScene();
        void EndScene();
        void FeedKey(const RendererPlatform::KeyboardKeyEventArgs& e);
        void AddRoom(std::int32_t id, GameMode mode = GameMode::None,
            std::int32_t playerCount = 0, BossFlags bossFlags = BossFlags::Unspecified,
            std::int32_t nodeLayerMask = 0, std::int32_t entityLayerId = -1);
        void AddRoom(std::string name, GameMode mode = GameMode::None,
            std::int32_t playerCount = 0, BossFlags bossFlags = BossFlags::Unspecified,
            std::int32_t nodeLayerMask = 0, std::int32_t entityLayerId = -1);
        void AddModel(std::string name, std::int32_t recolor = 0, bool firstHunt = false,
            MetaDir dir = MetaDir::Models,
            std::optional<OpenTK::Mathematics::Vector3> pos = std::nullopt);
        void AddPlayer(Hunter hunter, std::int32_t recolor = 0, std::int32_t team = -1,
            std::optional<OpenTK::Mathematics::Vector3> position = std::nullopt);
        void QueueMovie(std::int32_t movieId);
        void Run();
        // Switch the renderer without ending anything: the window's loop
        // stops after this frame, the window is remade on the other backend
        // and the match (or the launcher) carries on in it.
        void RequestRendererSwitch(NativeRuntime::Rhi::SceneBackendRequest request);
        // Called around a switch by whoever owns state on the old device.
        inline static std::function<void()> BeforeRendererSwitch{};
        inline static std::function<void(RenderWindow&)> AfterRendererSwitch{};
        // Diagnostics observe the exact transition, outside simulation: before
        // any GPU release (true), and after the complete rebuild (false).
        inline static std::function<void(RenderWindow&, bool)> ObserveRendererSwitch{};
        enum class RendererSwitchStage : std::uint8_t { BeforeWindow, Presentation, Resources, ReleasedResources, FinalRelease };
        // Diagnostic checkpoints include partial replacement resources. A
        // failure reporter may replace the modal dialog in a scripted run.
        inline static std::function<void(RenderWindow&, RendererSwitchStage)> ObserveRendererSwitchStage{};
        inline static std::function<void(std::exception_ptr, bool)> ReportRendererSwitchFailure{};

        // GameWindow's own window properties, which the C# RenderWindow has by
        // inheriting it.
        [[nodiscard]] std::int32_t WindowBorder() const;
        void WindowBorder(std::int32_t value);
        [[nodiscard]] OpenTK::Mathematics::Vector2i Location() const;
        void Location(OpenTK::Mathematics::Vector2i value);
        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientSize() const;
        void ClientSize(OpenTK::Mathematics::Vector2i value);
        [[nodiscard]] RendererPlatform::MonitorArea CurrentMonitorClientArea() const;
        [[nodiscard]] std::vector<RendererPlatform::MonitorArea> MonitorClientAreas() const;
        [[nodiscard]] RendererPlatform::WindowStateValue WindowState() const;
        void WindowStateMinimized();
        void WindowStateMaximized();
        void WindowStateNormal();
        [[nodiscard]] bool WindowStateFullscreen();
        [[nodiscard]] bool WindowStateBorderless();
        [[nodiscard]] double RefreshRate() const;
        void Floating(bool value);
        [[nodiscard]] bool IsFocused() const;
        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientLocation() const;
        void Focus();
        void Close();

        void OnClosing() override;
        void OnLoad() override;
        void OnRenderFrame(const RendererPlatform::FrameEventArgs& args) override;
        void OnResize(const RendererPlatform::ResizeEventArgs& e) override;
        void OnMove(const RendererPlatform::WindowPositionEventArgs& e) override;
        void OnMaximizedChanged(bool maximized) override;
        void OnFocusedChanged(bool focused) override;
        void OnMouseDown(const RendererPlatform::MouseButtonEventArgs& e) override;
        void OnMouseUp(const RendererPlatform::MouseButtonEventArgs& e) override;
        void OnMouseMove(const RendererPlatform::MouseMoveEventArgs& e) override;
        void OnMouseWheel(const RendererPlatform::MouseWheelEventArgs& e) override;
        void OnTextInput(const RendererPlatform::TextInputEventArgs& e) override;
        void OnKeyDown(const RendererPlatform::KeyboardKeyEventArgs& e) override;
        void OnKeyUp(const RendererPlatform::KeyboardKeyEventArgs& e) override;

    private:
        [[nodiscard]] static const RendererPlatform::WindowSettings& Settings();
        static bool OnWayland();
        static void IgnoreUnavailableGlfwFeatures();
        [[nodiscard]] std::shared_ptr<MphRead::Scene> NewScene();
        void EndOrClose();
        void Reveal();
        [[nodiscard]] std::pair<double, double> PointerPixels(double x, double y) const;
        void FitToScreen();
        void ApplyFrameRateSettings();
        void ReportReflexPacing(const NativeRuntime::Rhi::LowLatencyDiagnostics& reflex);
        // For what the window draws with no scene: the lobby's cleared frame,
        // the viewport after a resize.
        [[nodiscard]] NativeRuntime::Rhi::CommandList& WindowCommands();
        void SwitchRenderer(NativeRuntime::Rhi::SceneBackendRequest request);
        void CreateWindowOrFallBack();
        void CreatePresentation();
        std::optional<NativeRuntime::Rhi::SceneBackendRequest> _rendererSwitch{};

        static std::function<void(std::int32_t, std::string)> _glfwErrorCallback;
        static constexpr OpenTK::Mathematics::Vector2i _minimumSize{1024, 720};
        std::shared_ptr<RendererPlatform::Window> _window{};
        std::unique_ptr<NativeRuntime::Rhi::Swapchain> _swapchain{};
        std::unique_ptr<NativeRuntime::Rhi::CommandList> _windowCommands{};
        std::unique_ptr<MphRead::Mods::Diagnostics::FramePerformance> _performance{};
        std::shared_ptr<MphRead::Scene> _scene{};
        bool _shell = false;
        bool _sceneLoaded = false;
        bool _startedHidden = true;
        std::int32_t _applyStartupIn = 0;
        bool _sceneReady = false;
        std::int32_t _appliedFrameRateCap = -2;
        bool _appliedVSync = false;
        std::optional<NativeRuntime::Rhi::LowLatencyState> _reportedLatency;
        std::uint64_t _presentationMetricFrames = 0;
        bool BeforeFrame() override;
        bool CanSampleInputWhileWaiting() const override;
        void OnInputSample() override;
#if !defined(__ANDROID__)
        void OnInputEventsProcessed() override;
        void UpdateCursorCapture();
#endif
    };

}

#define MPHREAD_SCENE_RENDERER_MEMBERS \
public: \
    Scene(OpenTK::Mathematics::Vector2i size, \
        MphRead::RendererPlatform::KeyboardState& keyboardState, \
        MphRead::RendererPlatform::MouseState& mouseState, \
        std::function<void(std::string)> setTitle, std::function<void()> close); \
    [[nodiscard]] OpenTK::Mathematics::Vector2i Size() const noexcept; \
    void Size(OpenTK::Mathematics::Vector2i value) noexcept; \
    [[nodiscard]] OpenTK::Mathematics::Matrix4 PerspectiveMatrix() const noexcept; \
    [[nodiscard]] MphRead::CameraMode CameraMode() const noexcept; \
    [[nodiscard]] bool SideScene() const noexcept; \
    void SideScene(bool value) noexcept; \
    [[nodiscard]] bool ShowCursor() const; \
    [[nodiscard]] MphRead::Formats::Culling::FrustumInfo& FrustumInfo() const; \
    [[nodiscard]] bool FrameAdvance() const noexcept; \
    [[nodiscard]] bool FrameAdvanceLastFrame() const noexcept; \
    [[nodiscard]] bool ProcessFrame() const noexcept; \
    [[nodiscard]] bool Exiting() const noexcept; \
    [[nodiscard]] std::int32_t RoomId() const noexcept; \
    void RoomId(std::int32_t value) noexcept; \
    [[nodiscard]] std::int32_t AreaId() const noexcept; \
    void AreaId(std::int32_t value) noexcept; \
    [[nodiscard]] static MphRead::Language Language(); \
    static void Language(MphRead::Language value); \
    [[nodiscard]] OpenTK::Mathematics::Matrix4 ViewMatrix() const noexcept; \
    [[nodiscard]] OpenTK::Mathematics::Matrix4 ViewInvRotMatrix() const noexcept; \
    [[nodiscard]] OpenTK::Mathematics::Matrix4 ViewInvRotYMatrix() const noexcept; \
    [[nodiscard]] OpenTK::Mathematics::Vector3 CameraPosition() const noexcept; \
    [[nodiscard]] bool ShowNodeData() const noexcept; \
    [[nodiscard]] bool ShowInvisibleEntities() const noexcept; \
    [[nodiscard]] bool ShowAllEntities() const noexcept; \
    [[nodiscard]] bool TransformRoomNodes() const noexcept; \
    [[nodiscard]] bool ShowAllNodes() const noexcept; \
    void ShowAllNodes(bool value) noexcept; \
    [[nodiscard]] float FrameTime() const noexcept; \
    [[nodiscard]] std::uint64_t FrameCount() const noexcept; \
    [[nodiscard]] std::uint64_t LiveFrames() const noexcept; \
    [[nodiscard]] float ElapsedTime() const noexcept; \
    [[nodiscard]] float GlobalElapsedTime() const noexcept; \
    [[nodiscard]] MphRead::VolumeDisplay ShowVolumes() const noexcept; \
    [[nodiscard]] bool ShowForceFields() const noexcept; \
    [[nodiscard]] float KillHeight() const noexcept; \
    [[nodiscard]] bool ScanVisor() const; \
    [[nodiscard]] OpenTK::Mathematics::Vector3 Light1Vector() const noexcept; \
    [[nodiscard]] OpenTK::Mathematics::Vector3 Light1Color() const noexcept; \
    [[nodiscard]] OpenTK::Mathematics::Vector3 Light2Vector() const noexcept; \
    [[nodiscard]] OpenTK::Mathematics::Vector3 Light2Color() const noexcept; \
    [[nodiscard]] std::shared_ptr<MphRead::Entities::RoomEntity> Room() const noexcept; \
    [[nodiscard]] std::int32_t ActiveCutscene() const noexcept; \
    [[nodiscard]] bool AllowCameraMovement() const noexcept; \
    static constexpr std::int32_t DisplaySphereStacks = 16; \
    static constexpr std::int32_t DisplaySphereSectors = 24; \
    void AddRoom(std::string name, MphRead::GameMode mode = MphRead::GameMode::None, \
        std::int32_t playerCount = 0, MphRead::BossFlags bossFlags = MphRead::BossFlags::Unspecified, \
        std::int32_t nodeLayerMask = 0, std::int32_t entityLayerId = -1); \
    void SetRoomValues(const MphRead::RoomMetadata& meta); \
    std::shared_ptr<MphRead::Entities::EntityBase> AddModel(std::string name, std::int32_t recolor = 0, \
        bool firstHunt = false, MphRead::MetaDir dir = MphRead::MetaDir::Models, \
        std::optional<OpenTK::Mathematics::Vector3> pos = std::nullopt); \
    void AddPlayer(MphRead::Hunter hunter, std::int32_t recolor = 0, std::int32_t team = -1, \
        std::optional<OpenTK::Mathematics::Vector3> position = std::nullopt); \
    [[nodiscard]] MphRead::Formats::Culling::NodeRef UpdateNodeRef(MphRead::Formats::Culling::NodeRef current, \
        OpenTK::Mathematics::Vector3 prevPos, OpenTK::Mathematics::Vector3 curPos); \
    [[nodiscard]] MphRead::Formats::Culling::NodeRef GetNodeRefByName(std::string nodeName); \
    [[nodiscard]] bool PartCouldContain(std::int32_t partIndex, OpenTK::Mathematics::Vector3 position); \
    [[nodiscard]] MphRead::Formats::Culling::NodeRef GetNodeRefByPosition(OpenTK::Mathematics::Vector3 position); \
    [[nodiscard]] bool IsNodeRefVisible(MphRead::Formats::Culling::NodeRef nodeRef); \
    [[nodiscard]] bool IsNodeRefAudible(MphRead::Formats::Culling::NodeRef nodeRef); \
    void OnLoad(); \
    /* Release every GPU resource the scene owns -- textures, targets, */ \
    /* framebuffers, meshes, shaders -- and wait until the device has */ \
    /* destroyed them, in the context the scene drew with. */ \
    void ReleaseGpuResources(); \
    void ReleaseGpuForSwitch(); \
    void RebuildGpuAfterSwitch(const std::function<void()>& checkpoint = {}); \
    void RebindInput(MphRead::RendererPlatform::KeyboardState& keyboard, MphRead::RendererPlatform::MouseState& mouse) noexcept; \
    void KeepTextureCopy(std::int32_t bindingId, std::int32_t width, std::int32_t height, \
        MphRead::NativeRuntime::Rhi::TextureFormat format, const void* pixels, bool owned); \
    [[nodiscard]] bool IsRoomModel(const Model* model) const; \
    void InitEntity(const std::shared_ptr<MphRead::Entities::EntityBase>& entity); \
    [[nodiscard]] OpenTK::Mathematics::Vector2i RenderSize() const; \
    void OnResize(); \
    void LoadModel(std::string name, bool firstHunt = false); \
    void LoadModel(const std::shared_ptr<MphRead::Model>& model, bool isRoom = false); \
    [[nodiscard]] std::int32_t BindGetTexture(const std::shared_ptr<MphRead::Model>& model, \
        std::int32_t textureId, std::int32_t paletteId, std::int32_t recolorId); \
    [[nodiscard]] std::int32_t BindGetTexture(const std::vector<MphRead::ColorRgba>& data, \
        std::int32_t width, std::int32_t height); \
    void BindTexture(const std::vector<MphRead::ColorRgba>& data, std::int32_t width, \
        std::int32_t height, std::int32_t bindingId); \
    void UpdateMaterials(const std::shared_ptr<MphRead::Model>& model, std::int32_t recolorId); \
    [[nodiscard]] static bool BreakNextFrame() noexcept; \
    static void BreakNextFrame(bool value) noexcept; \
    void OnUpdateFrame(); \
    void OnSimulationFrame(); \
    void OnDrawFrame(); \
    [[nodiscard]] OpenTK::Mathematics::Matrix4 GetPerspectiveMatrix(float fov) const; \
    [[nodiscard]] static MphRead::Formats::Culling::FrustumPlane SetBoundsIndices(OpenTK::Mathematics::Vector4 plane); \
    /* the backend's own code for the offscreen target's completeness (GL: 0x8CD5 complete) */ \
    [[nodiscard]] std::int32_t FramebufferStatus() const noexcept; \
    /* the first pending device error, as the backend's own code (0: none) */ \
    [[nodiscard]] std::int32_t DrainGlError(); \
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadWindowBuffer(std::int32_t& width, std::int32_t& height); \
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadSceneTarget(std::int32_t& width, std::int32_t& height); \
    void AfterRenderFrame(); \
    [[nodiscard]] float FramesPerSecond() const noexcept; \
    [[nodiscard]] bool OnRenderFrame(); \
    void UnloadModel(const std::shared_ptr<MphRead::Model>& model); \
    void BeginModelReloadDrawProbe(); \
    [[nodiscard]] bool ModelReloadDrawProbePassed() const noexcept; \
    [[nodiscard]] std::string ModelReloadDrawProbeStatus() const; \
    void StartCutscene(std::int32_t id); \
    void EndCutscene(bool resetFade = false); \
    void ResetFrameCount(); \
    std::shared_ptr<MphRead::Entities::BeamEffectEntity> InitBeamEffect(const MphRead::Entities::BeamEffectEntityData& data); \
    void UnlinkBeamEffect(std::shared_ptr<MphRead::Entities::BeamEffectEntity> entry); \
    std::shared_ptr<MphRead::Entities::BombEntity> InitBomb(); \
    void UnlinkBomb(std::shared_ptr<MphRead::Entities::BombEntity> entry); \
    void AddSingleParticle(MphRead::SingleType type, OpenTK::Mathematics::Vector3 position, \
        OpenTK::Mathematics::Vector3 color, float alpha, float scale); \
    void UnlinkEffectEntry(const std::shared_ptr<MphRead::Effects::EffectEntry>& entry); \
    void DetachEffectEntry(const std::shared_ptr<MphRead::Effects::EffectEntry>& entry, bool setExpired); \
    void LoadEffect(std::int32_t effectId, bool persistent); \
    std::shared_ptr<MphRead::Effects::EffectEntry> SpawnEffectGetEntry(std::int32_t effectId, \
        OpenTK::Mathematics::Vector3 facing, OpenTK::Mathematics::Vector3 up, \
        OpenTK::Mathematics::Vector3 position, std::shared_ptr<MphRead::Formats::Collision::EntityCollision> entCol = nullptr); \
    std::shared_ptr<MphRead::Effects::EffectEntry> SpawnEffectGetEntry(std::int32_t effectId, \
        OpenTK::Mathematics::Matrix4 transform, std::shared_ptr<MphRead::Formats::Collision::EntityCollision> entCol = nullptr); \
    void SpawnEffect(std::int32_t effectId, OpenTK::Mathematics::Vector3 facing, \
        OpenTK::Mathematics::Vector3 up, OpenTK::Mathematics::Vector3 position, bool child = false, \
        std::shared_ptr<MphRead::Formats::Collision::EntityCollision> entCol = nullptr); \
    void SpawnEffect(std::int32_t effectId, OpenTK::Mathematics::Matrix4 transform, bool child = false, \
        std::shared_ptr<MphRead::Formats::Collision::EntityCollision> entCol = nullptr); \
    [[nodiscard]] std::int32_t CountElements(std::int32_t effectId); \
    void ClearEffects(); \
    void BreakEffectCycles(); \
    void ClearNonPersistentEffects(); \
    [[nodiscard]] std::int64_t ModEffectParticles() const noexcept; \
    void AddRenderItem(const MphRead::Material& material, std::int32_t polygonId, float alphaScale, \
        OpenTK::Mathematics::Vector3 emission, const MphRead::LightInfo& lightInfo, \
        OpenTK::Mathematics::Matrix4 texcoordMatrix, OpenTK::Mathematics::Matrix4 transform, \
        const std::shared_ptr<MphRead::Model>& model, const std::shared_ptr<MphRead::Mesh>& mesh, \
        std::int32_t matrixStackCount, const std::vector<float>& matrixStack, \
        std::optional<OpenTK::Mathematics::Vector4> overrideColor, \
        std::optional<OpenTK::Mathematics::Vector4> paletteOverride, MphRead::SelectionType selectionType, \
        MphRead::BillboardMode billboardMode, float scaleFactor = 1.0F, \
        std::optional<std::int32_t> bindingOverride = std::nullopt); \
    void AddRenderItem(MphRead::CullingMode cullingMode, std::int32_t polygonId, \
        OpenTK::Mathematics::Vector4 overrideColor, MphRead::RenderItemType type, \
        std::shared_ptr<MphRead::ManagedArray<OpenTK::Mathematics::Vector3>> vertices, std::int32_t vertexCount = 0, bool noLines = false); \
    void AddRenderItem(MphRead::RenderItemType type, float alpha, std::int32_t polygonId, \
        OpenTK::Mathematics::Vector3 color, MphRead::RepeatMode xRepeat, MphRead::RepeatMode yRepeat, \
        float scaleS, float scaleT, OpenTK::Mathematics::Matrix4 transform, \
        std::shared_ptr<MphRead::ManagedArray<OpenTK::Mathematics::Vector3>> uvsAndVerts, std::int32_t bindingId, \
        MphRead::BillboardMode billboardMode = MphRead::BillboardMode::None, std::int32_t trailCount = 8); \
    void AddRenderItem(MphRead::RenderItemType type, std::int32_t polygonId, \
        OpenTK::Mathematics::Vector3 color, MphRead::RepeatMode xRepeat, MphRead::RepeatMode yRepeat, \
        float scaleS, float scaleT, std::int32_t matrixStackCount, const std::vector<float>& matrixStack, \
        std::shared_ptr<MphRead::ManagedArray<OpenTK::Mathematics::Vector3>> uvsAndVerts, std::int32_t segmentCount, std::int32_t bindingId); \
    [[nodiscard]] std::int32_t GetNextPolygonId(); \
    void InitLoadedEntity(std::int32_t count); \
    [[nodiscard]] MphRead::FadeType FadeType() const noexcept; \
    void SetFade(MphRead::FadeType type, float length, bool overwrite, \
        MphRead::AfterFade afterFade = MphRead::AfterFade::None, float delay = 0.0F); \
    void DoCleanup(); \
    [[nodiscard]] std::shared_ptr<MphRead::Hud::LayerInfo> Layer1Info() const noexcept; \
    [[nodiscard]] std::shared_ptr<MphRead::Hud::LayerInfo> Layer2Info() const noexcept; \
    [[nodiscard]] std::shared_ptr<MphRead::Hud::LayerInfo> Layer3Info() const noexcept; \
    [[nodiscard]] std::shared_ptr<MphRead::Hud::LayerInfo> Layer4Info() const noexcept; \
    [[nodiscard]] std::shared_ptr<MphRead::Hud::LayerInfo> Layer5Info() const noexcept; \
    void DrawCustomCrosshair(OpenTK::Mathematics::Vector3 color, float posX = 0.5F, float posY = 0.5F); \
    void DrawHitMarker(OpenTK::Mathematics::Vector4 color, float posX = 0.5F, float posY = 0.5F); \
    void DrawHudTexture(float left, float top, float right, float bottom, std::int32_t bindingId, \
        float alpha = 1.0F, bool smooth = true); \
    void DrawFlatDisc(float posX, float posY, OpenTK::Mathematics::Vector2 localCenter, float radius, \
        OpenTK::Mathematics::Vector4 color, std::int32_t segments = 32); \
    void DrawFlatRing(float posX, float posY, OpenTK::Mathematics::Vector2 localCenter, float radius, \
        float thickness, OpenTK::Mathematics::Vector4 color, std::int32_t segments = 48); \
    void DrawFlatLine(float posX, float posY, OpenTK::Mathematics::Vector2 from, OpenTK::Mathematics::Vector2 to, \
        float thickness, OpenTK::Mathematics::Vector4 color); \
    void DrawFlatSquare(float posX, float posY, OpenTK::Mathematics::Vector2 localCenter, float halfSize, \
        OpenTK::Mathematics::Vector4 color); \
    void DrawFlatPolygon(float posX, float posY, OpenTK::Mathematics::Vector2 localCenter, \
        std::span<const OpenTK::Mathematics::Vector2> localPoints, OpenTK::Mathematics::Vector4 color); \
    void DrawHudFlatBox(float left, float top, float right, float bottom, OpenTK::Mathematics::Vector4 color); \
    void DrawHudObject(const std::shared_ptr<MphRead::Hud::HudObjectInstance>& inst, std::int32_t mode = 0, float scale = 1.0F); \
    void DrawIconModel(OpenTK::Mathematics::Vector2 position, float angle, \
        const std::shared_ptr<MphRead::ModelInstance>& inst, MphRead::ColorRgb color, float alpha, float scaleMult = 1.0F); \
    void DrawHudFilterModel(const std::shared_ptr<MphRead::ModelInstance>& inst, float alpha = 1.0F); \
    void DrawHudDamageModel(const std::shared_ptr<MphRead::ModelInstance>& inst); \
    void LookAt(OpenTK::Mathematics::Vector3 target); \
    [[nodiscard]] bool IsFreeCam() const noexcept; \
    void SetFreeCamera(bool on); \
    void ToggleFreeCamera(); \
    void OnMouseClick(bool down); \
    void OnMouseMove(float deltaX, float deltaY); \
    void OnMouseWheel(float offsetY); \
    [[nodiscard]] bool ShowCollision() const noexcept; \
    [[nodiscard]] MphRead::EntityType ColEntDisplay() const noexcept; \
    [[nodiscard]] MphRead::Terrain ColTerDisplay() const noexcept; \
    [[nodiscard]] MphRead::CollisionType ColTypeDisplay() const noexcept; \
    [[nodiscard]] MphRead::CollisionColor ColDisplayColor() const noexcept; \
    [[nodiscard]] float ColDisplayAlpha() const noexcept; \
    void OnKeyDown(const MphRead::RendererPlatform::KeyboardKeyEventArgs& e); \
    MphRead::RendererConcurrentQueue<std::shared_ptr<MphRead::Entities::EntityBase>>& LoadedEntities() noexcept; \
    [[nodiscard]] bool InitEntities() const noexcept; \
    void InitEntities(bool value) noexcept; \
private: \
    friend class MphRead::Entities::BeamEffectEntity; \
    friend class MphRead::Entities::BombEntity; \
    void UnlinkBeamEffect(MphRead::Entities::BeamEffectEntity* entry); \
    void UnlinkBomb(MphRead::Entities::BombEntity* entry); \
    struct FlatColor final \
    { \
        float _red = 0.0F; \
        float _green = 0.0F; \
        float _blue = 0.0F; \
        float _weight = 0.0F; \
        float _count = 0.0F; \
        float _plainRed = 0.0F; \
        float _plainGreen = 0.0F; \
        float _plainBlue = 0.0F; \
        void Add(MphRead::ColorRgba pixel); \
        [[nodiscard]] OpenTK::Mathematics::Vector3 Result() const; \
    }; \
    class MovieFadeSettings \
    { \
    public: \
        MphRead::Movie MovieId{}; \
        std::optional<MphRead::Movie> AfterMovieId{}; \
        MphRead::FadeType AfterFadeType{}; \
        float AfterFadeLength = 0.0F; \
        std::optional<OpenTK::Mathematics::Vector3> AfterPosition{}; \
        std::optional<OpenTK::Mathematics::Vector3> AfterFacing{}; \
        MphRead::AfterMovie AfterMovieAction{}; \
    }; \
    enum class InputMode : std::int32_t \
    { \
        All, \
        PlayerOnly, \
        CameraOnly \
    }; \
    enum class PromptState : std::int32_t \
    { \
        None, \
        Load, \
        CameraPos \
    }; \
    void SetShaderFog(); \
    void InitShaders(); \
    void GenerateGpuMeshes(const std::shared_ptr<MphRead::Model>& model, bool isRoom); \
    void DrawGpuMesh(const std::shared_ptr<MphRead::Model>& model, const std::shared_ptr<MphRead::Mesh>& mesh); \
    void UnloadModel(const std::shared_ptr<MphRead::Model>& model, bool removeReadCache); \
    void InitTextures(const std::shared_ptr<MphRead::Model>& model); \
    std::pair<std::int32_t, bool> BindTexture(const std::shared_ptr<MphRead::Model>& model, \
        std::int32_t textureId, std::int32_t paletteId, std::int32_t recolorId); \
    static OpenTK::Mathematics::Vector3 AverageOf(const std::vector<MphRead::ColorRgba>& data); \
    void UpdateMaterial(MphRead::Material& material, bool onlyOpaque); \
    void ModStepDrawPassTimers(); \
    void UpdateProjection(); \
    void UpdateDepthAttachment(OpenTK::Mathematics::Vector2i target); \
    float MeasureDepthQuantum(); \
    void DrawCelOutline(); \
    [[nodiscard]] MphRead::NativeRuntime::Rhi::GraphicsDevice& Gpu(); \
    [[nodiscard]] MphRead::NativeRuntime::Rhi::GraphicsPipelineDesc DescribeScenePass( \
        MphRead::ScenePass pass) const; \
    [[nodiscard]] const MphRead::NativeRuntime::Rhi::GraphicsPipeline& ScenePipeline( \
        MphRead::ScenePass pass, MphRead::NativeRuntime::Rhi::CullMode cull, \
        MphRead::NativeRuntime::Rhi::FillMode fill, std::int32_t lineWidth); \
    void BeginScenePass(MphRead::ScenePass pass); \
    [[nodiscard]] MphRead::NativeRuntime::Rhi::CommandList& Commands(); \
    void CreateSceneTargets(OpenTK::Mathematics::Vector2i size); \
    [[nodiscard]] MphRead::NativeRuntime::Rhi::RenderingInfo SceneRenderingInfo( \
        std::array<MphRead::NativeRuntime::Rhi::RenderingColorAttachment, 1>& color, \
        MphRead::NativeRuntime::Rhi::RenderingDepthStencilAttachment& depth) const; \
    void BeginSceneRendering( \
        MphRead::NativeRuntime::Rhi::LoadOp color = MphRead::NativeRuntime::Rhi::LoadOp::Load, \
        MphRead::NativeRuntime::Rhi::LoadOp depth = MphRead::NativeRuntime::Rhi::LoadOp::Load, \
        MphRead::NativeRuntime::Rhi::LoadOp stencil = MphRead::NativeRuntime::Rhi::LoadOp::Load, \
        MphRead::NativeRuntime::Rhi::ClearColor clearColor = {}, \
        MphRead::NativeRuntime::Rhi::Scissor area = {}); \
    void BeginCelRendering(); \
    void BeginWindowRendering( \
        MphRead::NativeRuntime::Rhi::LoadOp color = MphRead::NativeRuntime::Rhi::LoadOp::Load, \
        MphRead::NativeRuntime::Rhi::LoadOp depth = MphRead::NativeRuntime::Rhi::LoadOp::Load, \
        MphRead::NativeRuntime::Rhi::ClearColor clearColor = {}, \
        MphRead::NativeRuntime::Rhi::Scissor area = {}); \
    [[nodiscard]] MphRead::NativeRuntime::Rhi::ClearColor SceneClearColor() const; \
    [[nodiscard]] static MphRead::NativeRuntime::Rhi::TextureUsage SceneDepthCopyUsage(); \
    [[nodiscard]] MphRead::NativeRuntime::Rhi::Texture* TextureFor(std::int32_t bindingId) const; \
    [[nodiscard]] const MphRead::NativeRuntime::Rhi::Sampler& SamplerFor(bool linear, \
        MphRead::RepeatMode s, MphRead::RepeatMode t); \
    void BindSceneTexture(std::uint32_t slot, std::int32_t bindingId, \
        const MphRead::NativeRuntime::Rhi::Sampler& sampler); \
    void BindSceneTexture(std::uint32_t slot, const MphRead::NativeRuntime::Rhi::Texture& texture, \
        const MphRead::NativeRuntime::Rhi::Sampler& sampler); \
    void UnbindSceneTexture(std::uint32_t slot); \
    [[nodiscard]] std::int32_t CreateOwnedTexture(std::int32_t width, std::int32_t height, \
        MphRead::NativeRuntime::Rhi::TextureFormat format, const void* pixels); \
    void WriteOwnedTexture(std::int32_t bindingId, std::int32_t width, std::int32_t height, \
        MphRead::NativeRuntime::Rhi::TextureFormat format, const void* pixels); \
    void DrawCelQuad(OpenTK::Mathematics::Vector2i target, bool probe); \
    void DrawCelPicture(OpenTK::Mathematics::Vector2i target, bool probe); \
    void CalibrateInk(OpenTK::Mathematics::Vector2i target); \
    void CountFrame(); \
    void LoadAndUnload(); \
    void UnloadEntity(const std::shared_ptr<MphRead::Entities::EntityBase>& entity); \
    void TransformCamera(); \
    void UpdateCameraPosition(); \
    void ResetCamera(); \
    void UpdateCameraRotation(float stepH, float stepV); \
    void AllocateEffects(); \
    std::shared_ptr<MphRead::Effects::EffectEntry> InitEffectEntry(); \
    std::shared_ptr<MphRead::Effects::EffectElementEntry> InitEffectElement(const std::shared_ptr<MphRead::Effect>& effect, \
        const std::shared_ptr<MphRead::EffectElement>& element, std::shared_ptr<MphRead::Formats::Collision::EntityCollision> entCol, bool child); \
    void UnlinkEffectElement(std::shared_ptr<MphRead::Effects::EffectElementEntry> element); \
    static void ReleaseFromOwner(const std::shared_ptr<MphRead::Effects::EffectElementEntry>& element); \
    std::shared_ptr<MphRead::Effects::EffectParticle> InitEffectParticle(); \
    void UnlinkEffectParticle(const std::shared_ptr<MphRead::Effects::EffectParticle>& particle); \
    void SpawnEffect(std::int32_t effectId, OpenTK::Mathematics::Matrix4 transform, bool child, \
        const std::shared_ptr<MphRead::Effects::EffectEntry>& entry, std::shared_ptr<MphRead::Formats::Collision::EntityCollision> entCol); \
    void ProcessEffects(std::uint64_t effectFrame); \
    std::shared_ptr<MphRead::RenderItem> GetRenderItem(); \
    void AddRenderItem(const std::shared_ptr<MphRead::RenderItem>& item); \
    void UpdateScene(); \
    void GetDrawItems(); \
    void UpdateUniforms(); \
    void UseRoomLights(); \
    void UseLight1(OpenTK::Mathematics::Vector3 vector, OpenTK::Mathematics::Vector3 color); \
    void UseLight2(OpenTK::Mathematics::Vector3 vector, OpenTK::Mathematics::Vector3 color); \
    void SetMatrixStack(const OpenTK::Mathematics::Matrix4& transform); \
    void SetFrameMatrices(const OpenTK::Mathematics::Matrix4& view, const OpenTK::Mathematics::Matrix4& projection); \
    void UpdateFade(); \
    void QuitGame(bool enteringShip); \
    void EndFade(); \
    void RenderItem(const std::shared_ptr<MphRead::RenderItem>& item); \
    void BeginTransient(MphRead::TransientPrimitiveTopology topology); \
    void TransientVertex3(float x, float y, float z); \
    void TransientVertex3(OpenTK::Mathematics::Vector3 vector); \
    void TransientTexCoord3(float s, float t, float r); \
    void TransientTexCoord3(OpenTK::Mathematics::Vector3 coord); \
    void EndTransient(); \
    void RenderBox(const MphRead::ManagedArray<OpenTK::Mathematics::Vector3>& verts); \
    void RenderCylinder(const MphRead::ManagedArray<OpenTK::Mathematics::Vector3>& verts); \
    void RenderSphere(const MphRead::ManagedArray<OpenTK::Mathematics::Vector3>& verts); \
    void RenderQuad(const MphRead::ManagedArray<OpenTK::Mathematics::Vector3>& verts); \
    void RenderNgon(const MphRead::ManagedArray<OpenTK::Mathematics::Vector3>& verts, std::int32_t count); \
    void RenderNgonLines(const MphRead::ManagedArray<OpenTK::Mathematics::Vector3>& verts, std::int32_t count); \
    void RenderParticle(const MphRead::RenderItem& item); \
    void RenderTrailSingle(const MphRead::RenderItem& item); \
    void RenderTrailMulti(const MphRead::RenderItem& item); \
    void RenderTrailStack(const MphRead::RenderItem& item); \
    void SetPauseMenuUniforms(); \
    void SetHudLayerUniforms(); \
    void UnsetHudLayerUniforms(); \
    void DrawHudLayer(const std::shared_ptr<MphRead::Hud::LayerInfo>& info); \
    void DoMaterial(const MphRead::RenderItem& item); \
    void DoTexture(const MphRead::RenderItem& item); \
    void SetFlatColor(std::int32_t bindingId); \
    [[nodiscard]] bool ScoreboardOverFreeCamera() const; \
    void OnKeyHeld(); \
    void MoveRoamCamera(); \
    void UpdatePointModule(); \
    void OutputStart(); \
    void OutputStop(); \
    void OutputUpdate(MphRead::RendererStopToken token); \
    void OutputLoadPrompt(); \
    /* OutputCameraPrompt preserves managed Single.TryParse grammar in Renderer.cpp. */ \
    void OutputCameraPrompt(); \
    std::string OutputGetAll(); \
    void OutputGetBotAi(); \
    void OutputGetCollisionMenu(); \
    void OutputGetMenu(); \
    void OutputGetEntityInfo(); \
    void OutputGetModel(); \
    void OutputGetNode(); \
    void OutputGetMesh(); \
    std::string OnOff(bool setting); \
    std::string YesNo(bool setting); \
    [[nodiscard]] bool FilteringOn() const; \
    void FilteringOn(bool value); \
    [[nodiscard]] bool LightingOn() const; \
    void LightingOn(bool value); \
    [[nodiscard]] bool FogOn() const; \
    void FogOn(bool value); \
    OpenTK::Mathematics::Vector2i _rendererSize{}; \
    OpenTK::Mathematics::Matrix4 _viewMatrix = MphRead::RendererDetail::IdentityMatrix(); \
    OpenTK::Mathematics::Matrix4 _viewInvRotMatrix = MphRead::RendererDetail::IdentityMatrix(); \
    OpenTK::Mathematics::Matrix4 _viewInvRotYMatrix = MphRead::RendererDetail::IdentityMatrix(); \
    OpenTK::Mathematics::Matrix4 _perspectiveMatrix = MphRead::RendererDetail::IdentityMatrix(); \
    MphRead::CameraMode _cameraMode = MphRead::CameraMode::Pivot; \
    bool _sideScene = false; \
    float _pivotAngleY = 0.0F; \
    float _pivotAngleX = 0.0F; \
    float _pivotDistance = 5.0F; \
    OpenTK::Mathematics::Vector3 _cameraPosition{}; \
    OpenTK::Mathematics::Vector3 _cameraFacing{0.0F, 0.0F, -1.0F}; \
    OpenTK::Mathematics::Vector3 _cameraUp{0.0F, 1.0F, 0.0F}; \
    OpenTK::Mathematics::Vector3 _cameraRight{1.0F, 0.0F, 0.0F}; \
    float _cameraFov = 1.361356816555577F; \
    bool _leftMouse = false; \
    std::int32_t _activeCutscene = -1; \
    OpenTK::Mathematics::Vector3 _priorCameraPos{}; \
    OpenTK::Mathematics::Vector3 _priorCameraFacing{0.0F, 0.0F, -1.0F}; \
    float _priorCameraFov = 1.361356816555577F; \
    std::shared_ptr<MphRead::Formats::Culling::FrustumInfo> _frustumInfo{}; \
    bool _showTextures = true; \
    bool _showColors = true; \
    std::int32_t _wireframeLevel = 0; \
    static constexpr std::int32_t MaxWireframeLevel = 5; \
    std::int32_t _volumeEdges = 0; \
    bool _faceCulling = true; \
    bool _scanVisor = false; \
    std::int32_t _showInvisible = 0; \
    bool _showNodeData = false; \
    MphRead::VolumeDisplay _showVolumes = MphRead::VolumeDisplay::None; \
    std::int32_t _showBotAiSlot = -1; \
    bool _showCollision = false; \
    bool _showAllNodes = false; \
    bool _transformRoomNodes = false; \
    bool _outputCameraPos = false; \
    std::unordered_map<std::int32_t, std::shared_ptr<MphRead::TextureMap>> _texPalMap{}; \
    std::unordered_map<std::int32_t, std::unique_ptr<MphRead::NativeRuntime::Rhi::Texture>> _ownedTextures{}; \
    std::unordered_map<std::int32_t, MphRead::SceneTextureCopy> _textureCopies{}; \
    std::unordered_map<std::int32_t, MphRead::SceneModelTextureSource> _modelTextureSources{}; \
    MphRead::GpuMeshCache _gpuMeshCache{}; \
    bool _modelReloadProbeRequested = false; \
    bool _modelReloadProbeAwaitingRedraw = false; \
    bool _modelReloadProbePassed = false; \
    bool _modelReloadProbeFailed = false; \
    std::uint64_t _modelReloadProbeReloadFrame = 0; \
    std::weak_ptr<MphRead::Model> _modelReloadProbeModel{}; \
    std::weak_ptr<MphRead::Mesh> _modelReloadProbeMesh{}; \
    std::string _modelReloadProbeStatus{"inactive"}; \
    std::shared_ptr<MphRead::TransientGeometryResource> _transientGeometry{}; \
    std::vector<MphRead::TransientVertex> _transientVertices{}; \
    MphRead::TransientPrimitiveTopology _transientTopology = MphRead::TransientPrimitiveTopology::Triangles; \
    OpenTK::Mathematics::Vector3 _transientTexCoord{}; \
    bool _transientHasTexCoords = false; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::SceneShaderSet> _sceneShaders{}; \
    inline static MphRead::NativeRuntime::Rhi::NullShaderConstantSink _noShaderConstants{}; \
    MphRead::NativeRuntime::Rhi::ShaderConstantSink* _shaderConstants = &_noShaderConstants; \
    OpenTK::Mathematics::Vector3 _light1Vector{}; \
    OpenTK::Mathematics::Vector3 _light1Color{}; \
    OpenTK::Mathematics::Vector3 _light2Vector{}; \
    OpenTK::Mathematics::Vector3 _light2Color{}; \
    bool _hasFog = false; \
    OpenTK::Mathematics::Vector4 _fogColor{}; \
    std::int32_t _fogOffset = 0; \
    std::int32_t _fogSlope = 0; \
    OpenTK::Mathematics::Vector4 _clearColor{0.0F, 0.0F, 0.0F, 1.0F}; \
    const float _nearClip = 0.0625F; \
    float _farClip = 0.0F; \
    bool _useClip = false; \
    float _killHeight = 0.0F; \
    float _frameTime = 0.0F; \
    float _elapsedTime = 0.0F; \
    float _globalElapsedTime = 0.0F; \
    std::uint64_t _frameCount = 0; \
    std::uint64_t _liveFrames = 0; \
    bool _frameAdvanceOn = false; \
    bool _frameAdvanceLastFrame = false; \
    bool _advanceOneFrame = false; \
    bool _recording = false; \
    std::int32_t _framesRecorded = 0; \
    bool _exiting = false; \
    bool _roomLoaded = false; \
    std::shared_ptr<MphRead::Entities::RoomEntity> _room{}; \
    std::int32_t _roomId = -1; \
    std::int32_t _areaId = -1; \
    inline static MphRead::Language _language = MphRead::Language::English; \
    MphRead::RendererPlatform::KeyboardState* _keyboardState = nullptr; \
    MphRead::RendererPlatform::MouseState* _mouseState = nullptr; \
    std::function<void(std::string)> _setTitle{}; \
    std::function<void()> _close{}; \
    MphRead::NativeRuntime::Rhi::GraphicsDevice* _gpu = nullptr; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::CommandList> _commands{}; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::Texture> _sceneColor{}; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::TextureView> _sceneColorView{}; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::Texture> _sceneDepthStencil{}; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::TextureView> _sceneDepthStencilView{}; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::Texture> _celColor{}; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::TextureView> _celColorView{}; \
    /* This frame's finished picture is in CelColor, the outline drawn there from SceneColor. */ \
    bool _celOutlined = false; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::Texture> _celDepth{}; \
    std::unique_ptr<MphRead::NativeRuntime::Rhi::TextureView> _celDepthView{}; \
    std::array<std::unique_ptr<MphRead::NativeRuntime::Rhi::Sampler>, 18> _samplers{}; \
    bool _depthTextureRefused = false; \
    MphRead::ScenePass _itemPass = MphRead::ScenePass::Opaque; \
    bool _previewIntoWindow = false; \
    std::unordered_map<std::uint32_t, std::unique_ptr<MphRead::NativeRuntime::Rhi::GraphicsPipeline>> _pipelines{}; \
    /* The pass pipelines last asked for: every item asks for one. */ \
    std::array<std::pair<std::uint32_t, const MphRead::NativeRuntime::Rhi::GraphicsPipeline*>, 8> _recentPipelines{}; \
    std::size_t _recentPipelineNext = 0; \
    OpenTK::Mathematics::Vector2i _targetSize{}; \
    std::unordered_map<std::int32_t, OpenTK::Mathematics::Vector3> _flatColors{}; \
    inline static bool _breakNextFrame = false; \
    std::int32_t _framebufferStatus = 0x8CD5; \
    inline static bool _saidDepthSize = false; \
    float _claimedQuantum = 5.960464832810452E-8F; \
    float _depthQuantum = 5.960464832810452E-8F; \
    bool _calibrateInk = true; \
    float _framesPerSecond = 0.0F; \
    std::chrono::steady_clock::time_point _fpsClock = std::chrono::steady_clock::now(); \
    std::int32_t _fpsFrames = 0; \
    static constexpr float _almostHalfPi = 1.5707953267948966F; \
    static constexpr std::int32_t _effectEntryMax = 64; \
    static constexpr std::int32_t _effectElementMax = 96; \
    static constexpr std::int32_t _effectParticleMax = 200; \
    static constexpr std::int32_t _singleParticleMax = 200; \
    static constexpr std::int32_t _beamEffectMax = 100; \
    static constexpr std::int32_t _bombMax = 32; \
    std::queue<std::shared_ptr<MphRead::Effects::EffectEntry>> _inactiveEffects{}; \
    std::queue<std::shared_ptr<MphRead::Effects::EffectElementEntry>> _inactiveElements{}; \
    std::vector<std::shared_ptr<MphRead::Effects::EffectElementEntry>> _activeElements{}; \
    std::queue<std::shared_ptr<MphRead::Effects::EffectParticle>> _inactiveParticles{}; \
    std::int32_t _singleParticleCount = 0; \
    std::vector<std::shared_ptr<MphRead::Effects::SingleParticle>> _singleParticles{}; \
    std::queue<std::shared_ptr<MphRead::Entities::BeamEffectEntity>> _inactiveBeamEffects{}; \
    std::vector<std::shared_ptr<MphRead::Entities::BeamEffectEntity>> _activeBeamEffects{}; \
    std::queue<std::shared_ptr<MphRead::Entities::BombEntity>> _inactiveBombs{}; \
    std::vector<std::shared_ptr<MphRead::Entities::BombEntity>> _activeBombs{}; \
    static constexpr std::int32_t _renderItemAlloc = 200; \
    std::queue<std::shared_ptr<MphRead::RenderItem>> _freeRenderItems{}; \
    std::deque<std::shared_ptr<MphRead::RenderItem>> _usedRenderItems{}; \
    std::int32_t _pendingEffectSteps = 0; \
    std::uint64_t _effectFrame = 0; \
    std::int64_t _modEffectParticles = 0; \
    std::int32_t _pendingFadeSteps = 0; \
    std::vector<std::shared_ptr<MphRead::RenderItem>> _decalItems{}; \
    std::vector<std::shared_ptr<MphRead::RenderItem>> _nonDecalItems{}; \
    std::vector<std::shared_ptr<MphRead::RenderItem>> _translucentItems{}; \
    std::array<float, 16> _scaleFactors{}; \
    std::int32_t _nextPolygonId = 1; \
    MphRead::RendererConcurrentQueue<std::shared_ptr<MphRead::Entities::EntityBase>> _loadedEntities{}; \
    bool _initEntities = false; \
    MovieFadeSettings _movieSettings{}; \
    MphRead::FadeType _fadeType = MphRead::FadeType::None; \
    float _fadeColor = 0.0F; \
    bool _fadeIn = false; \
    float _fadeStart = 0.0F; \
    float _fadeLength = 0.0F; \
    float _fadePercent = 0.0F; \
    float _fadeDelay = 0.0F; \
    bool _fadeEnded = false; \
    MphRead::AfterFade _afterFade = MphRead::AfterFade::None; \
    std::shared_ptr<MphRead::Hud::LayerInfo> _layer1Info{}; \
    std::shared_ptr<MphRead::Hud::LayerInfo> _layer2Info{}; \
    std::shared_ptr<MphRead::Hud::LayerInfo> _layer3Info{}; \
    std::shared_ptr<MphRead::Hud::LayerInfo> _layer4Info{}; \
    std::shared_ptr<MphRead::Hud::LayerInfo> _layer5Info{}; \
    std::array<float, 16 * 31> _hudMatrixStack{}; \
    bool _freeCam = false; \
    /* The spectator free camera moves once per 60 Hz step, with the world. */ \
    float _roamMouseX = 0, _roamMouseY = 0; \
    float _roamPadMoveX = 0, _roamPadMoveY = 0, _roamPadRise = 0, _roamPadLookX = 0, _roamPadLookY = 0; \
    MphRead::EntityType _colEntDisplay = MphRead::EntityType::Room; \
    MphRead::Terrain _colTerDisplay = MphRead::Terrain::All; \
    MphRead::CollisionType _colTypeDisplay = MphRead::CollisionType::Any; \
    MphRead::CollisionColor _colDisplayColor = MphRead::CollisionColor::None; \
    float _colDisplayAlpha = 0.5F; \
    std::int32_t _colMenuSelect = 0; \
    InputMode _inputMode = InputMode::All; \
    PromptState _promptState = PromptState::None; \
    MphRead::RendererConcurrentQueue<std::tuple<std::string, std::int32_t, bool>> _loadQueue{}; \
    MphRead::RendererConcurrentQueue<std::shared_ptr<MphRead::Entities::EntityBase>> _unloadQueue{}; \
    MphRead::RendererJThread _outputThread{}; \
    std::string _currentOutput{}; \
    std::string _outputBuffer{};
