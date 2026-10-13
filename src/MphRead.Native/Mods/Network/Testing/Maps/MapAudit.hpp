#pragma once

#include "Renderer.hpp"

#include "Formats/Culling.hpp"
#include "Formats/Enums.hpp"
#include "Formats/Types.hpp"
#include "Scene.hpp"
#include "Entities/EntityBase.hpp"
#include "Entities/Players/PlayerEntity.hpp"


#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MphRead
{
    class CollisionVolume;
    class Scene;
    enum class GameMode : std::uint8_t;

    namespace Entities
    {
        class EntityBase;
        class PlayerEntity;
    }
}

namespace MphRead::Mods::Network
{
    class MapAudit final : public MphRead::RendererPlatform::WindowEvents
    {
    private:
        std::shared_ptr<MphRead::RendererPlatform::Window> _window;
        std::unique_ptr<MphRead::NativeRuntime::Rhi::Swapchain> _swapchain;

        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientSize() const;
        void Close();
        void Present();

    public:
        void Run();
        // GameWindow.Dispose: releases the window; the object stays usable.
        void Dispose();

    private:
        std::string _room;
        std::int32_t _players;
        double _seconds;
        std::int32_t _frame = 0;
        std::int32_t _spawned = 0;
        std::array<std::int32_t, Entities::PlayerEntity::SlotCapacity> _deaths{};
        std::array<std::int32_t, Entities::PlayerEntity::SlotCapacity> _lastHealth{};
        std::array<bool, Entities::PlayerEntity::SlotCapacity> _everSpawned{};
        std::array<bool, Entities::PlayerEntity::SlotCapacity> _everAltForm{};
        std::array<bool, Entities::PlayerEntity::SlotCapacity> _everFired{};
        std::array<OpenTK::Mathematics::Vector3, Entities::PlayerEntity::SlotCapacity> _lastSeen{};
        std::array<bool, Entities::PlayerEntity::SlotCapacity> _haveLastSeen{};
        std::array<float, Entities::PlayerEntity::SlotCapacity> _travelled{};
        std::array<bool, Entities::PlayerEntity::SlotCapacity> _everFrozen{};
        std::array<bool, Entities::PlayerEntity::SlotCapacity> _everBurned{};
        std::array<bool, Entities::PlayerEntity::SlotCapacity> _everDisrupted{};
        double _lowestY = std::numeric_limits<double>::max();

        std::optional<std::string> _shotDirectory{};
        std::int32_t _shotsSaved = 0;
        std::int32_t _litSamples = 0;
        double _litMin = std::numeric_limits<double>::max();
        double _litMax = 0.0;
        double _litFirst = -1.0;
        double _litTotal = 0.0;
        static constexpr std::int32_t _litSampleFrames = 60;
        static constexpr double _renderFloor = 0.06;

        const bool _renderProbe;
        const bool _itemProbe;
        std::vector<OpenTK::Mathematics::Vector3> _itemSpots{};
        std::vector<std::string> _itemNames{};
        std::int32_t _itemIndex = -1;
        std::int32_t _itemFrames = 0;
        static constexpr float _itemStandOff = 2.2F;
        static constexpr std::int32_t _itemSettleFrames = 24;

        std::vector<OpenTK::Mathematics::Vector3> _spawnSpots{};
        std::vector<OpenTK::Mathematics::Vector3> _spawnFacings{};
        std::vector<Formats::Culling::NodeRef> _spawnNodeRefs{};
        std::int32_t _spawnIndex = -1;
        std::int32_t _spawnFrames = 0;
        double _spawnLitAtSpawn = 0.0;
        double _spawnLitWorst = 0.0;
        std::int32_t _spawnFailures = 0;
        static constexpr std::int32_t _spawnSettleFrames = 30;
        static constexpr std::int32_t _spawnWalkFrames = 300;

        std::int32_t _nodeLookupSamples = 0;
        std::int32_t _nodeLookupNone = 0;
        std::int32_t _nodeLookupWrong = 0;
        std::int32_t _nodeLookupHidden = 0;
        std::int32_t _nodeLookupShown = 0;
        std::int32_t _nodeLookupWalkedVisible = 0;

        std::array<Formats::Culling::NodeRef, Entities::PlayerEntity::SlotCapacity> _puppetNode{};
        std::array<OpenTK::Mathematics::Vector3, Entities::PlayerEntity::SlotCapacity> _puppetPrev{};
        std::array<bool, Entities::PlayerEntity::SlotCapacity> _puppetSeeded{};
        std::int32_t _puppetSamples = 0;
        std::int32_t _puppetNone = 0;
        std::int32_t _puppetWrong = 0;
        std::int32_t _puppetHidden = 0;

        std::vector<std::shared_ptr<Entities::EntityBase>> _probeTargets{};
        std::int32_t _probeIndex = -1;
        std::int32_t _probeFrames = 0;
        std::int32_t _probeWait = 0;
        bool _probePlaced = false;
        std::int32_t _probeAttempt = 0;
        std::int32_t _probeMarkPads = 0;
        std::int32_t _probeMarkTeleports = 0;
        std::int32_t _padsProbed = 0;
        std::int32_t _padsFired = 0;
        std::int32_t _telesProbed = 0;
        std::int32_t _telesFired = 0;
        static constexpr std::int32_t _probeSlot = 0;
        static constexpr std::int32_t _probeFrameLimit = 12;
        static constexpr std::int32_t _probeRetryFrameLimit = 45;
        static constexpr std::int32_t _probeMaxTargets = 24;

        static const std::array<std::pair<Hunter, std::string_view>, 3> _afflictions;
        std::int32_t _afflictIndex = -1;
        std::int32_t _afflictFrames = 0;
        std::int32_t _afflictShooter = -1;
        std::int32_t _afflictVictim = -1;
        std::array<bool, 3> _afflictTried{};
        std::array<bool, 3> _afflictLanded{};
        std::array<bool, 3> _afflictFired{};
        std::array<bool, 3> _afflictHit{};
        std::int32_t _afflictVictimHealth = 0;
        std::int32_t _afflictWait = 0;
        Affliction _afflictShotAfflictions = Affliction::None;
        std::int32_t _afflictMaxCharge = 0;
        std::int32_t _afflictSetUpWait = 0;
        static constexpr std::int32_t _afflictFrameLimit = 320;

        std::int32_t _drawAdvancedTheGame = 0;
        std::int32_t _lockjawTrailChecks = 0;
        std::int32_t _lockjawTrailMismatches = 0;
        std::int32_t _lockjawDrawRngChanges = 0;

        static MphRead::RendererPlatform::WindowSettings GameSettings();
        static MphRead::RendererPlatform::WindowSettings WindowSettings();

        static bool _showWindow;
        static std::int32_t _drawRate;
        static std::optional<OpenTK::Mathematics::Vector2i> _windowSize;
        static bool _forceEveryone;
        static bool _diagnostic;
        static std::optional<Hunter> _mainHunter;

        std::unique_ptr<MphRead::Scene> _scene;
        const bool _bots;

        MapAudit(
            std::string room,
            std::int32_t players,
            double seconds,
            GameMode mode,
            bool bots,
            bool renderProbe,
            bool itemProbe);

    public:
        MapAudit(const MapAudit&) = delete;
        MapAudit& operator=(const MapAudit&) = delete;
        MapAudit(MapAudit&&) = delete;
        MapAudit& operator=(MapAudit&&) = delete;
        ~MapAudit() override;

        [[nodiscard]] static bool ShowWindow() noexcept;
        static void ShowWindow(bool value) noexcept;

        [[nodiscard]] static std::int32_t DrawRate() noexcept;
        static void DrawRate(std::int32_t value) noexcept;

        [[nodiscard]] static std::optional<OpenTK::Mathematics::Vector2i> WindowSize() noexcept;
        static void WindowSize(std::optional<OpenTK::Mathematics::Vector2i> value) noexcept;

        [[nodiscard]] MphRead::Scene& Scene() noexcept;
        [[nodiscard]] const MphRead::Scene& Scene() const noexcept;

        [[nodiscard]] static bool ForceEveryone() noexcept;
        // Internal-equivalent setter used by this assembly/module.
        static void ForceEveryone(bool value) noexcept;

        [[nodiscard]] static bool Diagnostic() noexcept;
        static void Diagnostic(bool value) noexcept;

    protected:
        void OnLoad() override;
        void OnRenderFrame(const MphRead::RendererPlatform::FrameEventArgs& args) override;

    private:
        void Drive();
        [[nodiscard]] bool StepProbe();
        [[nodiscard]] bool StepAfflictionProbe();
        [[nodiscard]] static bool Alive(Entities::PlayerEntity& player);
        [[nodiscard]] bool SetUpAffliction(Hunter hunter);
        [[nodiscard]] static OpenTK::Mathematics::Vector3 VolumeCenter(const CollisionVolume& volume);
        [[nodiscard]] static OpenTK::Mathematics::Vector3 TriggerPoint(Entities::EntityBase& entity);
        void CollectProbeTargets();
        void StepScoreboard();

        std::int32_t _scoreboardFrames = 0;

        void Observe();
        void SampleNodeLookup(Entities::PlayerEntity& player);
        void SamplePuppetNode(Entities::PlayerEntity& player);
        [[nodiscard]] bool StepSpawnRender();
        [[nodiscard]] bool StepItemShots();
        void CollectItemSpots();
        void SaveSpawnShot(std::string_view what);
        void CollectSpawnSpots();
        void SampleRender();

    protected:
        void OnClosing() override;

    private:
        [[nodiscard]] std::int32_t Report();

        // MapAuditTeams.cs
    public:
        [[nodiscard]] static bool TeamProbe() noexcept { return _teamProbe; }
        static void TeamProbe(bool value) noexcept { _teamProbe = value; }

    private:
        inline static bool _teamProbe = false;
        std::vector<std::string> _teamProbeProblems{};
        void CaptureTeamResults();
        [[nodiscard]] std::vector<std::string> RunTeamProbe();

    public:
        [[nodiscard]] static std::optional<Hunter> MainHunter() noexcept;
        static void MainHunter(std::optional<Hunter> value) noexcept;

        [[nodiscard]] static std::int32_t Run(
            std::string room,
            std::int32_t players,
            double seconds,
            GameMode mode,
            bool bots = false,
            std::optional<std::string> shotDirectory = std::nullopt,
            bool renderProbe = false,
            bool allNodes = false,
            bool itemProbe = false);
    };
}
