#pragma once

#include "Renderer.hpp"

#include "Formats/Enums.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Input/SyntheticInput.hpp"


#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead
{
    class Scene;
    enum class GameMode : std::uint8_t;
}

namespace MphRead::Mods::Network
{
    class NetFeatureCheck;

    class NetCheckClient final : public MphRead::RendererPlatform::WindowEvents
    {
    private:
        std::shared_ptr<MphRead::RendererPlatform::Window> _window;
        std::unique_ptr<MphRead::NativeRuntime::Rhi::Swapchain> _swapchain;
        Mods::Input::KeyboardState _headlessKeyboard{};
        Mods::Input::MouseState _headlessMouse{};
        bool _headlessClosing = false;
        void RunHeadless();
        void HeadlessFrame();

        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientSize() const;
        void Close();
        void Present();

    public:
        void Run();
        // GameWindow.Dispose: releases the window; the object stays usable.
        void Dispose();

    private:
        class RemoteView final
        {
        public:
            std::int32_t FramesActive = 0;
            std::int32_t FramesSpawned = 0;
            std::int32_t FirstSpawnFrame = -1;
            std::int32_t Deaths = 0;
            std::int32_t Hits = 0;
            std::int32_t MinHealth = std::numeric_limits<std::int32_t>::max();
            std::int32_t LastHealth = -1;
            bool WasAlive = false;
            double Travelled = 0.0;
            std::int32_t DistinctPositions = 0;
            OpenTK::Mathematics::Vector3 LastPosition{};
            bool HavePosition = false;
            std::int32_t AltFormFrames = 0;
            std::int32_t AltFormWantedFrames = 0;
            std::int32_t AltFormUnavailableFrames = 0;
            std::int32_t AltFormDisagreeFrames = 0;
            MphRead::Hunter Hunter{};
        };

        std::string _name;
        std::optional<std::string> _shotDirectory{};
        double _seconds = 0.0;
        double _spectateAt = -1.0;
        double _rejoinAt = -1.0;
        std::chrono::steady_clock::time_point _wallClockStart{};
        float _scoreboardSampleAt = -1.0F;

        static constexpr float SampleEvery = 30.0F;
        std::int32_t _spectatingFrames = 0;
        std::int32_t _spectateStartedFrame = -1;
        std::int32_t _rejoinedFrame = -1;
        std::vector<std::int32_t> _remoteSpectatingFrames{};
        std::vector<std::unique_ptr<RemoteView>> _remotes{};
        std::int32_t _frame = 0;
        std::int32_t _shots = 0;
        std::int32_t _duelShots = 0;
        std::int32_t _lastDuelShotFrame = -1000;
        bool _opponentInView = false;
        std::int32_t _localSpawnFrame = -1;
        std::int32_t _minHealthSeen = std::numeric_limits<std::int32_t>::max();
        std::int32_t _lastLocalHealth = -1;
        std::int32_t _myDeaths = 0;
        std::int32_t _damageTaken = 0;
        bool _wasAliveLocal = false;
        std::int32_t _indicatorFrames = 0;
        double _litFraction = 0.0;
        std::int32_t _roomChanges = 0;
        std::int32_t _lastRoomId = -1;
        bool _everSawSomeone = false;
        std::int32_t _myAltFrames = 0;
        std::int32_t _myAuthorityAltFrames = 0;
        std::int32_t _myAuthorityFormSamples = 0;
        std::int32_t _myAuthorityFormDisagreeFrames = 0;
        bool _myAuthorityFormMismatchActive = false;
        std::unique_ptr<NetFeatureCheck> _features{};
        std::int32_t _featureFailures = 0;
        bool _votedOnce = false;
        bool _rebound = false;
        std::int32_t _endShots = 0;
        std::int32_t _mapVotesCast = 0;
        std::int32_t _mapVotesCarried = 0;
        std::string _lastBallotRoom{};
        [[nodiscard]] bool Capture(const std::string& path);
        void VoteOnMap();
        std::unique_ptr<MphRead::Scene> _scene{};

        [[nodiscard]] static MphRead::RendererPlatform::WindowSettings GameSettings();
        [[nodiscard]] static MphRead::RendererPlatform::WindowSettings WindowSettings(
            std::int32_t width, std::int32_t height);

        NetCheckClient(
            std::chrono::steady_clock::time_point wallClockStart,
            std::string name,
            std::string roomKey,
            MphRead::GameMode mode,
            MphRead::Hunter hunter,
            double seconds,
            std::optional<std::string> shotDirectory,
            std::int32_t width,
            std::int32_t height,
            double spectateAt = -1.0,
            double rejoinAt = -1.0,
            std::int32_t color = 0);

        void SampleScoreboardOnServerClock();
        void UpdateSpectating();
        void DriveVoteTest();
        void DriveRebindTest();
        void Observe();
        void SayHello();
        [[nodiscard]] bool Passed() const;
        [[nodiscard]] bool AnyoneSeen() const;
        [[nodiscard]] double FramesPerSecond() const;
        [[nodiscard]] double ElapsedSeconds() const;
        void Report();

    protected:
        void OnLoad() override;
        void OnRenderFrame(const MphRead::RendererPlatform::FrameEventArgs& args) override;
        void OnClosing() override;

    public:
        NetCheckClient(const NetCheckClient&) = delete;
        NetCheckClient(NetCheckClient&&) = delete;
        NetCheckClient& operator=(const NetCheckClient&) = delete;
        NetCheckClient& operator=(NetCheckClient&&) = delete;
        ~NetCheckClient() override;

        [[nodiscard]] MphRead::Scene& Scene() noexcept;
        [[nodiscard]] const MphRead::Scene& Scene() const noexcept;

        static void SetMapVoteRow(std::int32_t value) noexcept;
        static void SetShowWindow(bool value) noexcept;
        static void SetAiLevel(std::optional<std::int32_t> value) noexcept;
        static void SetHeadless(bool value) noexcept;

    private:
        inline static std::int32_t MapVoteRow = -1;
        inline static bool ShowWindow = false;
        // -ai [LEVEL]: the game's own AI plays this client's player, in place
        // of the scripted tour -- a fighting opponent on a real line.
        inline static std::optional<std::int32_t> AiLevel{};
        // -headless: no window, no GPU, the simulation stepped at 60 Hz by
        // the wall clock -- what the dedicated server does. A test client then
        // needs no display, and cannot fail on a driver that has nothing to
        // do with the netcode under test.
        inline static bool Headless = false;

    public:
        [[nodiscard]] static std::int32_t Run(
            std::string host,
            std::int32_t port,
            std::string name,
            MphRead::Hunter hunter,
            double seconds,
            std::optional<std::string> shotDirectory,
            std::int32_t width,
            std::int32_t height,
            bool recordDemo = false,
            double spectateAt = -1.0,
            double rejoinAt = -1.0,
            std::int32_t color = -1);
    };
}
