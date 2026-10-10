#include "Renderer.hpp"
#include "NativeRuntime/System/ErrorDialog.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "Mods/Diagnostics/FramePerformance.hpp"
#include "RendererGeometry.hpp"
#include "NativeRuntime/System/Runtime.hpp"
#include "NativeRuntime/Rhi/SceneBackend.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/SceneGate.hpp"
#include "Scene.hpp"
#include "SceneSetup.hpp"
#include "GameState.hpp"
#include "Selection.hpp"
#include "Metadata/Metadata.hpp"
#include "Read.hpp"
#include "Shaders.hpp"
#include "Sound/Music.hpp"
#include "Sound/Sfx.hpp"
#include "Strings.hpp"
#include "Entities/EntityBase.hpp"
#include "Entities/RoomEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Entities/BeamEffectEntity.hpp"
#include "Entities/BombEntity.hpp"
#include "Entities/PlatformEntity.hpp"
#include "Entities/EnemyInstanceEntity.hpp"
#include "Entities/PointModuleEntity.hpp"
#include "Entities/LightSourceEntity.hpp"
#include "Entities/AreaVolumeEntity.hpp"
#include "Entities/TriggerVolumeEntity.hpp"
#include "Entities/EnemySpawnEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "Entities/ObjectEntity.hpp"
#include "Entities/CamSeq/CamSeqEntity.hpp"
#include "Formats/Effects.hpp"
#include "Formats/Model.hpp"
#include "Formats/Movie.hpp"
#include "HUD/HudInfo.hpp"
#include "Mods/Branding.hpp"
#include "Mods/EndScreen.hpp"
#include "Mods/GameSettings.hpp"
#include "Mods/Headless.hpp"
#include "Mods/InputSettings.hpp"
#include "Mods/Input/AimAssist/AimAssistDebug.hpp"
#include "Mods/Input/GamepadHaptics.hpp"
#include "Mods/Input/GamepadManager.hpp"
#include "Mods/Input/GamepadUiRouter.hpp"
#include "Mods/Input/PointerDevice.hpp"
#include "Mods/Input/SpectatorInput.hpp"
#include "Mods/Input/WindowsPenInput.hpp"
#include "Mods/MapPick.hpp"
#include "Mods/RenderOptions.hpp"
#include "Mods/SpectatorMode.hpp"
#include "Mods/ThumbnailMode.hpp"
#include "Mods/WindowGeometry.hpp"
#include "Mods/Chat/ChatBox.hpp"
#include "Mods/Input/GamepadInput.hpp"
#include "Mods/Input/InputSourceTracker.hpp"
#include "Mods/Input/PointerInput.hpp"
#include "Mods/Input/StylusZone.hpp"
#include "Mods/Input/WeaponWheel.hpp"
#include "Mods/Network/DemoClip.hpp"
#include "Mods/Network/DemoPlayback.hpp"
#include "Mods/Network/DemoRecorder.hpp"
#include "Mods/Network/NetHitClaims.hpp"
#include "Mods/Network/NetHitPrediction.hpp"
#include "Mods/Network/NetHooks.hpp"
#include "Mods/Network/NetSession.hpp"
#include "Mods/Network/NetLaunch.hpp"
#include "Mods/Render/Crosshair.hpp"
#include "Mods/Render/DesktopGlContext.hpp"
#include "Mods/Render/FrameTiming.hpp"
#include "Mods/Render/MapThumbnail.hpp"
#include "Mods/Render/LauncherHunter.hpp"
#include "Mods/Render/AppIcon.hpp"
#include "Export/Images.hpp"
#include "Features.hpp"
#include "Formats/Collision.hpp"
#include "Formats/CollisionDetection.hpp"
#include "Menu.hpp"
#include "Mods/DebugLog.hpp"
#include "Mods/Input/GamepadDesktop.hpp"
#include "Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "Mods/Network/MapVote.hpp"
#include "Mods/PauseMenu.hpp"
#include "Mods/WindowMode.hpp"
#include "Formats/Types.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#if defined(MPHREAD_SHELL)
#include "Mods/Input/KeyCapture.hpp"
#include "Mods/Launcher/Shell.hpp"
#include "Mods/Render/LauncherHunter.hpp"
#include "Mods/Render/UiOverlay.hpp"
#include "NativeRuntime/System/Encoding.hpp"
#endif

#include <algorithm>
#include <span>
#include <unordered_set>
#include <unordered_map>
#include <bit>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::RoundToEven;
using ::OpenTK::Mathematics::CreateRotationX;
using ::OpenTK::Mathematics::CreateRotationY;
using ::OpenTK::Mathematics::CreateRotationZ;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::CreateTranslation;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::MathHelper::DegreesToRadians;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::Negate;

using OpenTK::Mathematics::Matrix4;
using OpenTK::Mathematics::Vector2;
using OpenTK::Mathematics::Vector2i;
using OpenTK::Mathematics::Vector3;
using OpenTK::Mathematics::Vector4;

#if defined(DEBUG)
#define MPHREAD_DEBUG_ASSERT(condition) \
    ::MphRead::NativeRuntime::DebugAssert(static_cast<bool>(condition))
#else
#define MPHREAD_DEBUG_ASSERT(condition) ((void)0)
#endif

namespace
{
    constexpr float Pi = 3.14159265358979323846F;
    constexpr float TwoPi = Pi * 2.0F;

    template <typename T>
    void RemoveFirst(std::vector<std::shared_ptr<T>>& values, const std::shared_ptr<T>& value)
    {
        auto it = std::find(values.begin(), values.end(), value);
        if (it != values.end())
        {
            values.erase(it);
        }
    }

    // Diagnostic only, and only while the debug log is on: where each pooled
    // effect element and entry was last released, and which entries are
    // currently free, so a second release can name the first.
    struct EffectPoolTrace final
    {
        std::unordered_map<const void*, std::vector<void*>> LastRelease;
        std::unordered_set<const void*> FreeEntries;
    };

    EffectPoolTrace& PoolTrace()
    {
        static EffectPoolTrace& trace = *new EffectPoolTrace();
        return trace;
    }

    [[nodiscard]] std::string BoolOnOff(bool v) { return v ? "on" : "off"; }

    namespace GLMath
    {
    }

    template <typename E>
    [[nodiscard]] std::string EnumNumber(E value)
    {
        using U = std::underlying_type_t<E>;
        if constexpr (std::is_signed_v<U>)
        {
            return std::to_string(static_cast<long long>(static_cast<U>(value)));
        }
        return std::to_string(static_cast<unsigned long long>(static_cast<U>(value)));
    }

    [[nodiscard]] std::string GameModeText(MphRead::GameMode value)
    {
        using E = MphRead::GameMode;
        switch (value)
        {
        case E::None: return "None";
        case E::SinglePlayer: return "SinglePlayer";
        case E::Battle: return "Battle";
        case E::BattleTeams: return "BattleTeams";
        case E::Survival: return "Survival";
        case E::SurvivalTeams: return "SurvivalTeams";
        case E::Capture: return "Capture";
        case E::Bounty: return "Bounty";
        case E::BountyTeams: return "BountyTeams";
        case E::Nodes: return "Nodes";
        case E::NodesTeams: return "NodesTeams";
        case E::Defender: return "Defender";
        case E::DefenderTeams: return "DefenderTeams";
        case E::PrimeHunter: return "PrimeHunter";
        case E::Unknown15: return "Unknown15";
        }
        return EnumNumber(value);
    }

    [[nodiscard]] std::string FramebufferErrorText(std::int32_t value)
    {
        return value == 0x8CD5 ? "complete" : "the device will not render to these attachments";
    }

#define MPH_ENUM_CASE(type, name) case type::name: return #name

    [[nodiscard]] std::string EnumText(MphRead::EntityType value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::CollisionType value)
    {
        using E = MphRead::CollisionType;
        switch (value)
        {
            MPH_ENUM_CASE(E, Any); MPH_ENUM_CASE(E, Player); MPH_ENUM_CASE(E, Beam); MPH_ENUM_CASE(E, Both);
        }
        return EnumNumber(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::CollisionColor value)
    {
        using E = MphRead::CollisionColor;
        switch (value)
        {
            MPH_ENUM_CASE(E, None); MPH_ENUM_CASE(E, Entity); MPH_ENUM_CASE(E, Terrain); MPH_ENUM_CASE(E, Type);
        }
        return EnumNumber(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::Terrain value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::Hunter value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::ItemType value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::EnemyType value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::TriggerType value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::FhTriggerType value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::Message value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::FhMessage value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::BillboardMode value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::RenderMode value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::PolygonMode value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::TexgenMode value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::RepeatMode value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] std::string BoolText(bool value)
    {
        return value ? "True" : "False";
    }

    [[nodiscard]] std::string EnumText(MphRead::Entities::TriggerFlags value)
    {
        return ::MphRead::Entities::ToString(value);
    }

    [[nodiscard]] std::string EnumText(MphRead::Entities::FhTriggerFlags value)
    {
        return ::MphRead::Entities::ToString(value);
    }

#undef MPH_ENUM_CASE

}

namespace MphRead
{
    using Entities::LoadFlags;
    using Export::Images;
    using Formats::CollisionDetection;
    using Formats::TestFlags;

    // WindowStartMode.ToString().
    [[nodiscard]] std::string WindowStartModeName(Mods::WindowStartMode mode)
    {
        switch (mode)
        {
        case Mods::WindowStartMode::Windowed:
            return "Windowed";
        case Mods::WindowStartMode::BorderlessFullscreen:
            return "BorderlessFullscreen";
        case Mods::WindowStartMode::ExclusiveFullscreen:
            return "ExclusiveFullscreen";
        }
        return std::to_string(static_cast<std::int32_t>(mode));
    }

    using Effects::EffectEntry;
    using Effects::EffectElementEntry;
    using Effects::EffectParticle;
    using Effects::SingleParticle;
    using Effects::EffElemFlags;
    using Effects::TimeValues;
    using Hud::LayerInfo;
    using Hud::HudObjectInstance;
    bool Scene::BreakNextFrame() noexcept { return _breakNextFrame; }
    void Scene::BreakNextFrame(bool value) noexcept { _breakNextFrame = value; }

    Scene::Scene(Vector2i size, RendererPlatform::KeyboardState& keyboardState,
        RendererPlatform::MouseState& mouseState, std::function<void(std::string)> setTitle,
        std::function<void()> close)
        : _rendererSize(size),
          _frustumInfo(std::make_shared<MphRead::Formats::Culling::FrustumInfo>()),
          _keyboardState(&keyboardState),
          _mouseState(&mouseState),
          _setTitle(std::move(setTitle)),
          _close(std::move(close)),
          _layer1Info(std::make_shared<LayerInfo>()),
          _layer2Info(std::make_shared<LayerInfo>()),
          _layer3Info(std::make_shared<LayerInfo>()),
          _layer4Info(std::make_shared<LayerInfo>()),
          _layer5Info(std::make_shared<LayerInfo>())
    {
        Read::ClearCache();
        Text::Strings::ClearCache();
        GameState::Reset();
        Entities::PlayerEntity::Construct(this);
        Music::Init();
    }

    Scene::Scene(PointerCheckTag)
    {
        // RuntimeHelpers.GetUninitializedObject in PointerCheck.cs skips the
        // normal scene setup and its cache, game-state, player and music resets.
    }

    Vector2i Scene::Size() const noexcept { return _rendererSize; }
    void Scene::Size(Vector2i value) noexcept { _rendererSize = value; }
    Matrix4 Scene::PerspectiveMatrix() const noexcept { return _perspectiveMatrix; }
    CameraMode Scene::CameraMode() const noexcept { return _cameraMode; }
    bool Scene::SideScene() const noexcept { return _sideScene; }
    void Scene::SideScene(bool value) noexcept { _sideScene = value; }
    bool Scene::ShowCursor() const
    {
        const auto main = Entities::PlayerEntity::Main();
        return Mods::Input::WeaponWheel::Absolute() && main
            && TypeExtensions::TestFlag(main->Flags1(), Entities::PlayerFlags1::WeaponMenuOpen);
    }
    MphRead::Formats::Culling::FrustumInfo& Scene::FrustumInfo() const { return *_frustumInfo; }
    bool Scene::FrameAdvance() const noexcept { return _frameAdvanceOn; }
    bool Scene::FrameAdvanceLastFrame() const noexcept { return _frameAdvanceLastFrame; }
    bool Scene::ProcessFrame() const noexcept
    {
        return (_frameCount == 0 || !_frameAdvanceOn || _advanceOneFrame) && !_exiting;
    }
    bool Scene::Exiting() const noexcept { return _exiting; }
    std::int32_t Scene::RoomId() const noexcept { return _roomId; }
    void Scene::RoomId(std::int32_t value) noexcept { _roomId = value; }
    std::int32_t Scene::AreaId() const noexcept { return _areaId; }
    void Scene::AreaId(std::int32_t value) noexcept { _areaId = value; }
    MphRead::Language Scene::Language()
    {
        return Paths::IsMphKorea() ? MphRead::Language::Japanese : _language;
    }
    void Scene::Language(MphRead::Language value) { _language = value; }
    Matrix4 Scene::ViewMatrix() const noexcept { return _viewMatrix; }
    Matrix4 Scene::ViewInvRotMatrix() const noexcept { return _viewInvRotMatrix; }
    Matrix4 Scene::ViewInvRotYMatrix() const noexcept { return _viewInvRotYMatrix; }
    Vector3 Scene::CameraPosition() const noexcept { return _cameraPosition; }
    bool Scene::ShowNodeData() const noexcept { return _showNodeData; }
    bool Scene::ShowInvisibleEntities() const noexcept { return _showInvisible != 0; }
    bool Scene::ShowAllEntities() const noexcept { return _showInvisible == 2; }
    bool Scene::TransformRoomNodes() const noexcept { return _transformRoomNodes; }
    bool Scene::ShowAllNodes() const noexcept { return _showAllNodes; }
    void Scene::ShowAllNodes(bool value) noexcept { _showAllNodes = value; }
    float Scene::FrameTime() const noexcept { return _frameTime; }
    std::uint64_t Scene::FrameCount() const noexcept { return _frameCount; }
    std::uint64_t Scene::LiveFrames() const noexcept { return _liveFrames; }
    float Scene::ElapsedTime() const noexcept { return _elapsedTime; }
    float Scene::GlobalElapsedTime() const noexcept { return _globalElapsedTime; }
    VolumeDisplay Scene::ShowVolumes() const noexcept { return _showVolumes; }
    bool Scene::ShowForceFields() const noexcept { return _showVolumes != VolumeDisplay::Portal; }
    float Scene::KillHeight() const noexcept { return _killHeight; }
    bool Scene::ScanVisor() const
    {
        return _cameraMode == CameraMode::Player ? Entities::PlayerEntity::Main()->ScanVisor() : _scanVisor;
    }
    Vector3 Scene::Light1Vector() const noexcept { return _light1Vector; }
    Vector3 Scene::Light1Color() const noexcept { return _light1Color; }
    Vector3 Scene::Light2Vector() const noexcept { return _light2Vector; }
    Vector3 Scene::Light2Color() const noexcept { return _light2Color; }
    std::shared_ptr<Entities::RoomEntity> Scene::Room() const noexcept { return _room; }
    std::int32_t Scene::ActiveCutscene() const noexcept { return _activeCutscene; }
    bool Scene::AllowCameraMovement() const noexcept
    {
        return _activeCutscene == -1 || (_frameAdvanceOn && !_advanceOneFrame);
    }

    bool Scene::FilteringOn() const { return Mods::RenderOptions::TextureFiltering(); }
    void Scene::FilteringOn(bool value) { Mods::RenderOptions::TextureFiltering(value); }
    bool Scene::LightingOn() const { return Mods::RenderOptions::Lighting(); }
    void Scene::LightingOn(bool value) { Mods::RenderOptions::Lighting(value); }
    bool Scene::FogOn() const { return Mods::RenderOptions::Fog(); }
    void Scene::FogOn(bool value) { Mods::RenderOptions::Fog(value); }

    void Scene::AddRoom(std::string name, GameMode mode, std::int32_t playerCount,
        BossFlags bossFlags, std::int32_t nodeLayerMask, std::int32_t entityLayerId)
    {
        if (_roomLoaded)
        {
            throw ProgramException("Cannot load more than one room in a scene.");
        }
        _roomLoaded = true;
        GameState::Mode(mode);
        Mods::DebugLog::Line("room", "loading \"" + name + "\" mode=" + GameModeText(mode)
            + " players=" + std::to_string(playerCount) + " layers=" + std::to_string(nodeLayerMask)
            + "/" + std::to_string(entityLayerId));
        [[maybe_unused]] auto loadStep = Mods::DebugLog::Step("room", "load \"" + name + "\"");
        auto [room, metaRef, collision, entitiesRef]
            = SceneSetup::LoadGame(name, this, playerCount, bossFlags, nodeLayerMask, entityLayerId);
        (void)collision;
        if (metaRef == nullptr)
        {
            throw System::NullReferenceException();
        }
        const RoomMetadata& meta = *metaRef;
        const auto& entities = RequireReference(entitiesRef);
        Mods::DebugLog::Line("room", "\"" + name + "\" read: " + std::to_string(entities.size())
            + " entit(ies), id=" + std::to_string(RoomId()) + ", area=" + std::to_string(AreaId()));
        RequireReference(::MphRead::GameState::StorySave).SetVisitedRoom(_roomId);
        RequireReference(::MphRead::GameState::StorySave).Areas = static_cast<std::uint16_t>(
            RequireReference(::MphRead::GameState::StorySave).Areas | static_cast<std::uint16_t>(1U << _areaId));
        if (GameState::Mode() == GameMode::None)
        {
            GameState::Mode(meta.Multiplayer ? GameMode::Battle : GameMode::SinglePlayer);
        }
        (void)_entities->AddFirst(room);
        InitEntity(room);
        _room = room;
        if (meta.InGameName.has_value())
        {
            _setTitle(meta.InGameName.value());
        }
        for (const auto& entity : entities)
        {
            InsertEntityByType(entity);
            MPHREAD_DEBUG_ASSERT(entity->Id != -1);
            _entityMap.Add(entity->Id, entity);
            InitEntity(entity);
            entity->Initialized = false;
        }
        SceneSetup::LoadItemResources(this);
        SceneSetup::LoadObjectResources(this);
        SceneSetup::LoadPlatformResources(this);
        SceneSetup::LoadEnemyResources(this);
        GameState::Setup(this);
        Entities::PlayerEntity::PlayerAiData::InitializeGlobals();
        if (GameState::Multiplayer())
        {
            Menu::ApplyMultiplayerSettings();
            Mods::GameSettings::ApplyMatchRules();
        }
        SetRoomValues(meta);
        for (const auto& player : Entities::PlayerEntity::Players())
        {
            if (((player->LoadFlags() & LoadFlags::SlotActive) == LoadFlags::SlotActive))
            {
                InsertEntityByType(player);
            }
        }
        for (const auto& player : Entities::PlayerEntity::Players())
        {
            if (player->IsBot())
            {
                player->AiData->InitializeAtLoad();
            }
        }
        auto main = Entities::PlayerEntity::Main();
        _cameraMode = Mods::Headless::Active() ? CameraMode::Roam
            : ((main->LoadFlags() & LoadFlags::Active) == LoadFlags::Active) ? CameraMode::Player : CameraMode::Roam;
        _inputMode = _cameraMode == CameraMode::Player ? InputMode::All : InputMode::CameraOnly;
        if (GameState::SinglePlayer() && !meta.FirstHunt && Entities::PlayerEntity::PlayerCount() > 0
            && !Cheats::SkipPlanetIntros())
        {
            Movie movieId = Movie::None;
            switch (_room->RoomId())
            {
            case 27: movieId = Movie::AlinosLanding; break;
            case 45: movieId = Movie::CALanding; break;
            case 65: movieId = Movie::VDOLanding; break;
            case 77: movieId = Movie::ArcterraLanding; break;
            case 89: movieId = Movie::OublietteLanding; break;
            default: break;
            }
            if (movieId != Movie::None)
            {
                _playingLandingMovie = true;
                Music::Pause();
                StartMovie(movieId, FadeType::FadeOutInBlack, 0.0F, FadeType::FadeOutWhite,
                    5.0F / 30.0F, AfterMovie::StartGame);
            }
        }
    }

    void Scene::SetRoomValues(const RoomMetadata& meta)
    {
        _light1Vector = meta.Light1Vector;
        _light1Color = Vector3(meta.Light1Color.Red / 31.0F, meta.Light1Color.Green / 31.0F,
            meta.Light1Color.Blue / 31.0F);
        _light2Vector = meta.Light2Vector;
        _light2Color = Vector3(meta.Light2Color.Red / 31.0F, meta.Light2Color.Green / 31.0F,
            meta.Light2Color.Blue / 31.0F);
        _hasFog = meta.FogEnabled;
        _fogColor = Vector4(meta.FogColor.Red / 31.0F, meta.FogColor.Green / 31.0F,
            meta.FogColor.Blue / 31.0F, 1.0F);
        _fogOffset = meta.FogOffset;
        _fogSlope = meta.FogSlope;
        if (meta.ClearFog && meta.FirstHunt)
        {
            _clearColor = Vector4(_fogColor.X, _fogColor.Y, _fogColor.Z, _fogColor.W);
        }
        _killHeight = meta.KillHeight;
        _farClip = meta.FarClip;
        if (_sceneShaders)
        {
            SetShaderFog();
        }
    }

    void Scene::SetShaderFog()
    {
        const float fogMin = _fogOffset / static_cast<float>(0x7FFF);
        const float fogMax = (_fogOffset + 32 * (0x400 >> _fogSlope)) / static_cast<float>(0x7FFF);
        _shaderConstants->Set(NativeRuntime::Rhi::SceneFogConstants{_fogColor, fogMin, fogMax});
    }

    std::shared_ptr<Entities::EntityBase> Scene::AddModel(std::string name, std::int32_t recolor,
        bool firstHunt, MetaDir dir, std::optional<Vector3> pos)
    {
        auto model = Read::GetModelInstance(name, firstHunt, dir);
        auto entity = std::make_shared<Entities::ModelEntity>(model, this, recolor);
        InsertEntityByType(entity);
        if (entity->Id != -1)
        {
            _entityMap.Add(entity->Id, entity);
        }
        InitEntity(entity);
        if (pos.has_value())
        {
            entity->Position = pos.value();
        }
        return entity;
    }

    void Scene::AddPlayer(Hunter hunter, std::int32_t recolor, std::int32_t team,
        std::optional<Vector3> position)
    {
        if (!_roomLoaded)
        {
            auto player = Entities::PlayerEntity::Create(hunter, recolor);
            if (player)
            {
                player->SetForcedSpawnPos(position);
                player->SetLoadFlags(player->LoadFlags() | LoadFlags::SlotActive);
                player->SetLoadFlags(player->LoadFlags() | LoadFlags::Active);
                player->SetLoadFlags(player->LoadFlags() | LoadFlags::Initial);
                if (team != -1)
                {
                    MPHREAD_DEBUG_ASSERT(team == 0 || team == 1);
                    player->SetTeamIndex(team);
                }
                player->SetIsBot(Entities::PlayerEntity::PlayerCount() >= 1);
                Entities::PlayerEntity::SetPlayerCount(Entities::PlayerEntity::PlayerCount() + 1);
            }
        }
    }

    Formats::Culling::NodeRef Scene::UpdateNodeRef(Formats::Culling::NodeRef current, Vector3 prevPos, Vector3 curPos)
    {
        return _room ? _room->UpdateNodeRef(current, prevPos, curPos) : Formats::Culling::NodeRef::None;
    }
    Formats::Culling::NodeRef Scene::GetNodeRefByName(std::string nodeName)
    {
        return _room ? _room->GetNodeRefByName(nodeName) : Formats::Culling::NodeRef::None;
    }
    bool Scene::PartCouldContain(std::int32_t partIndex, Vector3 position)
    {
        return _room ? _room->PartCouldContain(partIndex, position, 4.0F) : true;
    }
    Formats::Culling::NodeRef Scene::GetNodeRefByPosition(Vector3 position)
    {
        return _room ? _room->GetNodeRefByPosition(position) : Formats::Culling::NodeRef::None;
    }
    bool Scene::IsNodeRefVisible(Formats::Culling::NodeRef nodeRef)
    {
        return _room ? _room->IsNodeRefVisible(nodeRef) : false;
    }
    bool Scene::IsNodeRefAudible(Formats::Culling::NodeRef nodeRef)
    {
        return _room ? _room->IsNodeRefAudible(nodeRef) : false;
    }

    void Scene::OnLoad()
    {
        if (Mods::DebugLog::Active() && !Mods::Headless::Active())
        {
            const std::string adapter = Gpu().AdapterDescription();
            std::size_t start = 0;
            while (start <= adapter.size())
            {
                const std::size_t end = std::min(adapter.find('\n', start), adapter.size());
                Mods::DebugLog::Line("gl", adapter.substr(start, end - start));
                start = end + 1;
            }
        }
        if (!Mods::Headless::Active())
        {
            Commands().Begin();
            std::cout << "[render] field of view " << Mods::RenderOptions::FieldOfView() << " degrees"
                << (Mods::RenderOptions::FieldOfView() == Mods::RenderOptions::DefaultFov
                    ? " (the DS's own)" : "") << '\n';
            std::cout << "[render] cel shading " << (Mods::RenderOptions::CelShading() ? "on" : "off")
                << ", " << Mods::RenderOptions::CelBands() << " bands, outline "
                << NativeRuntime::ToStringInvariant(Mods::RenderOptions::CelEdge(), "0.00")
                << ", fog " << BoolOnOff(Mods::RenderOptions::Fog())
                << ", performance mode " << BoolOnOff(Mods::RenderOptions::PerformanceMode()
                    || Mods::ThumbnailMode::Active()) << '\n';
            InitShaders();
            _transientGeometry
                = NativeRuntime::Rhi::CreateSceneTransientGeometry(Gpu(), Commands());
        }
        AllocateEffects();
        CollisionDetection::Init();
        for (std::int32_t i = 0; i < _renderItemAlloc; ++i)
        {
            _freeRenderItems.push(std::make_shared<MphRead::RenderItem>());
        }
        auto e = Entities().GetEnumerator();
        while (e.MoveNext())
        {
            auto entity = e.Current();
            if (!entity->Initialized)
            {
                entity->Initialize();
                entity->Initialized = true;
            }
        }
        if (!_sideScene)
        {
            for (const auto& player : Entities::PlayerEntity::Players())
            {
                if (((player->LoadFlags() & LoadFlags::SlotActive) == LoadFlags::SlotActive))
                {
                    player->Initialize();
                    InitEntity(player);
                    InitEntity(player->Halfturret());
                }
            }
        }
        if (!Mods::Headless::Active() && !_sideScene && !Mods::ThumbnailMode::Active()
            && !NativeRuntime::ConsoleIsOutputRedirected()
            && !NativeRuntime::ConsoleIsInputRedirected())
        {
            OutputStart();
        }
        NativeRuntime::ForceFullGc();
        if (!NativeRuntime::IsAndroid())
        {
            NativeRuntime::SetSustainedLowLatencyGc();
        }
    }

    Vector2i Scene::RenderSize() const
    {
        return Vector2i(Mods::RenderOptions::Scaled(_rendererSize.X),
            Mods::RenderOptions::Scaled(_rendererSize.Y));
    }

    void Scene::OnResize()
    {
        if (!_sceneColor)
        {
            return;
        }
        const Vector2i target = RenderSize();
        _targetSize = target;
        const auto width = static_cast<std::uint32_t>(target.X);
        const auto height = static_cast<std::uint32_t>(target.Y);
        Gpu().ResizeTexture(*_sceneColor, width, height);
        if (_celColor)
        {
            Gpu().ResizeTexture(*_celColor, width, height);
        }
        MPHREAD_DEBUG_ASSERT(_sceneDepthStencil != nullptr);
        Gpu().ResizeTexture(*_sceneDepthStencil, width, height);
        if (_celDepth)
        {
            Gpu().ResizeTexture(*_celDepth, width, height);
        }
    }

    void Scene::InitShaders()
    {
        std::array<float, 64> shifts{};
        for (std::int32_t i = 0; i < 64; ++i)
        {
            const std::int32_t val = (i & 32) != 0 ? 31 - (i & 31) : i & 31;
            shifts[static_cast<std::size_t>(i)] = -((val - 16) << 12) / 4096.0F / 256.0F;
        }
        std::vector<float> toon;
        toon.reserve(Metadata::ToonTable.size() * 3);
        for (Vector3 vector : Metadata::ToonTable)
        {
            toon.push_back(vector.X);
            toon.push_back(vector.Y);
            toon.push_back(vector.Z);
        }
        NativeRuntime::Rhi::SceneShaderSources sources{};
        sources.MainVertex = &Shaders::VertexShader;
        sources.MainFragment = &Shaders::FragmentShader;
        sources.CompositeVertex = &Shaders::RttVertexShader;
        sources.CompositeFragment = &Shaders::RttFragmentShader;
        sources.ShiftFragment = &Shaders::ShiftFragmentShader;
        sources.CelFragment = &Shaders::CelFragmentShader;
        sources.ToonTable = toon;
        sources.ShiftTable = shifts;
        try
        {
            _sceneShaders = NativeRuntime::Rhi::CreateSceneShaderSet(Gpu(), Commands(), sources);
        }
        catch (const std::exception& ex)
        {
            throw ProgramException(ex.what());
        }
        _shaderConstants = &_sceneShaders->Constants();

        Vector2i renderTarget = RenderSize();
        _targetSize = renderTarget;
        CreateSceneTargets(renderTarget);
        {
            std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
            NativeRuntime::Rhi::RenderingDepthStencilAttachment depth{};
            const bool complete = Gpu().CanRender(SceneRenderingInfo(color, depth));
            _framebufferStatus = complete ? 0x8CD5 : 0x8CDD;
            if (!complete)
            {
                std::cout << "[render] the offscreen target is not usable: "
                    << FramebufferErrorText(_framebufferStatus)
                    << ". Nothing drawn into it will appear. Size " << _rendererSize.X << 'x' << _rendererSize.Y << ".\n";
                NativeRuntime::DebuggerBreak();
            }
        }
        SetShaderFog();
    }

    void Scene::InitEntity(const std::shared_ptr<Entities::EntityBase>& entity)
    {
        if (!entity)
        {
            throw System::NullReferenceException();
        }
        for (const auto& inst : entity->GetModels())
        {
            InitTextures(inst->Model());
            GenerateGpuMeshes(inst->Model(), entity->Type == EntityType::Room);
        }
    }

    void Scene::GenerateGpuMeshes(const std::shared_ptr<Model>& model, bool isRoom)
    {
        if (Mods::Headless::Active())
        {
            return;
        }
        for (const std::shared_ptr<Mesh>& meshValue : *model->Meshes)
        {
            Mesh& mesh = RequireReference(meshValue);
            if (_gpuMeshCache.Find(model.get(), meshValue.get()))
            {
                continue;
            }

            std::int32_t textureWidth = 0;
            std::int32_t textureHeight = 0;
            Material& material = *model->Materials->at(static_cast<std::size_t>(mesh.MaterialId));
            if (material.TextureId != -1)
            {
                const auto& recolor = model->Recolors->at(0);
                const auto& texture = recolor->Textures->at(static_cast<std::size_t>(material.TextureId));
                textureWidth = texture.Width;
                textureHeight = texture.Height;
            }

            const auto& list = model->RenderInstructionLists->at(
                static_cast<std::size_t>(mesh.DlistId));
            RendererGeometry geometry;
            try
            {
                geometry = DecodeRendererGeometry(*list, textureWidth, textureHeight,
                    material.TexgenMode == TexgenMode::Normal, isRoom,
                    RequireReference(model->NodeMatrixIds).size());
            }
            catch (const RendererGeometryException& ex)
            {
                throw ProgramException(ex.what());
            }

            const std::shared_ptr<const void> modelLifetime = model;
            const std::shared_ptr<const void> meshLifetime = meshValue;
            (void)_gpuMeshCache.GetOrCreate(modelLifetime, meshLifetime,
                [this, &geometry]()
                {
                    return NativeRuntime::Rhi::CreateSceneGpuMesh(Gpu(), Commands(), geometry);
                });
        }
    }

    void Scene::DrawGpuMesh(
        const std::shared_ptr<Model>& model, const std::shared_ptr<Mesh>& mesh)
    {
        if (!model || !mesh)
        {
            throw ProgramException("GPU mesh draw requires a live model and mesh.");
        }
        std::shared_ptr<GpuMeshResource> gpuMesh
            = _gpuMeshCache.Find(model.get(), mesh.get());
        if (!gpuMesh)
        {
            // Released for a renderer switch: made again from the model.
            GenerateGpuMeshes(model, IsRoomModel(model.get()));
            gpuMesh = _gpuMeshCache.Find(model.get(), mesh.get());
        }
        if (!gpuMesh)
        {
            throw ProgramException("GPU mesh cache entry is missing for model " + model->Name
                + ", mesh " + std::to_string(mesh->DlistId) + ", active room "
                + (_room ? _room->Meta().Name : "none") + ", frame " + std::to_string(_frameCount) + ".");
        }

        if (_modelReloadProbeAwaitingRedraw)
        {
            const std::shared_ptr<Model> probeModel = _modelReloadProbeModel.lock();
            const std::shared_ptr<Mesh> probeMesh = _modelReloadProbeMesh.lock();
            if (!probeModel || !probeMesh)
            {
                _modelReloadProbeAwaitingRedraw = false;
                _modelReloadProbeFailed = true;
                _modelReloadProbeStatus = "target model/mesh lifetime expired before redraw";
            }
            else if (model.get() == probeModel.get() && mesh.get() == probeMesh.get()
                && _frameCount > _modelReloadProbeReloadFrame)
            {
                // This is an ordinary production draw reached through RenderItem.
                gpuMesh->Draw();
                _modelReloadProbeAwaitingRedraw = false;
                _modelReloadProbePassed = true;
                _modelReloadProbeStatus = "redrew model " + model->Name
                    + " mesh " + std::to_string(mesh->DlistId)
                    + " on frame " + std::to_string(_frameCount)
                    + " after real GPU teardown/reload";
                return;
            }
        }

        if (_modelReloadProbeRequested && !_modelReloadProbeFailed)
        {
            bool isRoom = false;
            if (_room)
            {
                for (const std::shared_ptr<ModelInstance>& roomInst : _room->GetModels())
                {
                    if (roomInst && roomInst->Model().get() == model.get())
                    {
                        isRoom = true;
                        break;
                    }
                }
            }

            // The resource must already have rendered through the production path
            // before the gate is allowed to tear it down.
            std::weak_ptr<GpuMeshResource> retiredResource = gpuMesh;
            gpuMesh->Draw();
            gpuMesh.reset();

            _modelReloadProbeModel = model;
            _modelReloadProbeMesh = mesh;

            // Exercise the same Scene model GL teardown as production unload, but
            // keep the live Read cache entry: active entities still own this exact
            // Model object and replacing that cache entry would corrupt ownership.
            UnloadModel(model, false);

            const bool cacheEntryDestroyed
                = !_gpuMeshCache.Find(model.get(), mesh.get());
            const bool resourceDestroyed = retiredResource.expired();

            // Recreate textures and all mesh GPU resources with the same room
            // decoding mode that originally produced this model's geometry.
            LoadModel(model, isRoom);
            const bool cacheEntryReloaded
                = static_cast<bool>(_gpuMeshCache.Find(model.get(), mesh.get()));

            _modelReloadProbeRequested = false;
            if (!cacheEntryDestroyed || !resourceDestroyed || !cacheEntryReloaded)
            {
                _modelReloadProbeFailed = true;
                _modelReloadProbeStatus = "teardown/reload verification failed for model "
                    + model->Name + " mesh " + std::to_string(mesh->DlistId)
                    + " (cache_destroyed=" + (cacheEntryDestroyed ? std::string("true") : std::string("false"))
                    + ", resource_destroyed=" + (resourceDestroyed ? std::string("true") : std::string("false"))
                    + ", cache_reloaded=" + (cacheEntryReloaded ? std::string("true") : std::string("false"))
                    + ")";
                return;
            }

            _modelReloadProbeReloadFrame = _frameCount;
            _modelReloadProbeAwaitingRedraw = true;
            _modelReloadProbeStatus = "unloaded/reloaded model " + model->Name
                + " mesh " + std::to_string(mesh->DlistId)
                + (isRoom ? " as room geometry" : " as non-room geometry")
                + " on frame " + std::to_string(_frameCount)
                + "; waiting for a later production draw";
            return;
        }

        gpuMesh->Draw();
    }

    void Scene::BeginModelReloadDrawProbe()
    {
        _modelReloadProbeRequested = true;
        _modelReloadProbeAwaitingRedraw = false;
        _modelReloadProbePassed = false;
        _modelReloadProbeFailed = false;
        _modelReloadProbeReloadFrame = 0;
        _modelReloadProbeModel.reset();
        _modelReloadProbeMesh.reset();
        _modelReloadProbeStatus = "waiting for a production mesh draw";
    }

    bool Scene::ModelReloadDrawProbePassed() const noexcept
    {
        return _modelReloadProbePassed;
    }

    std::string Scene::ModelReloadDrawProbeStatus() const
    {
        return _modelReloadProbeStatus;
    }

    void Scene::LoadModel(std::string name, bool firstHunt)
    {
        LoadModel(Read::GetModelInstance(name, firstHunt)->Model(), false);
    }

    void Scene::LoadModel(const std::shared_ptr<Model>& model, bool isRoom)
    {
        InitTextures(model);
        GenerateGpuMeshes(model, isRoom);
    }

    void Scene::InitTextures(const std::shared_ptr<Model>& model)
    {
        if (Mods::Headless::Active())
        {
            return;
        }
        if (_texPalMap.contains(model->Id))
        {
            return;
        }
        struct Combo
        {
            std::int32_t Texture;
            std::int32_t Palette;
            std::int32_t Recolor;
            bool operator==(const Combo&) const = default;
        };
        struct ComboHash
        {
            std::size_t operator()(const Combo& c) const noexcept
            {
                return static_cast<std::uint32_t>(c.Texture)
                    ^ (static_cast<std::uint32_t>(c.Palette) << 12)
                    ^ (static_cast<std::uint32_t>(c.Recolor) << 24);
            }
        };
        std::unordered_set<Combo, ComboHash> combos;
        for (const auto& materialPtr : *model->Materials)
        {
            Material& material = *materialPtr;
            if (material.TextureId == -1)
            {
                continue;
            }
            if (material.RenderMode == RenderMode::Unknown3 || material.RenderMode == RenderMode::Unknown4)
            {
                material.RenderMode = RenderMode::Normal;
            }
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(model->Recolors->size()); ++i)
            {
                combos.insert({material.TextureId, material.PaletteId, i});
            }
        }
        for (const auto& group : *model->AnimationGroups->Texture)
        {
            for (const auto& pair : *group->Animations)
            {
                const auto& animation = pair.second;
                for (std::int32_t i = animation.StartIndex; i < animation.StartIndex + animation.Count; ++i)
                {
                    for (std::int32_t j = 0; j < static_cast<std::int32_t>(model->Recolors->size()); ++j)
                    {
                        combos.insert({group->TextureIds->at(static_cast<std::size_t>(i)),
                            group->PaletteIds->at(static_cast<std::size_t>(i)), j});
                    }
                }
            }
        }
        if (combos.empty() && !model->Recolors->empty()
            && !model->Recolors->at(0)->Textures->empty()
            && !model->Recolors->at(0)->Palettes->empty())
        {
            combos.insert({0, 0, 0});
        }
        if (!combos.empty())
        {
            auto map = std::make_shared<TextureMap>();
            for (const Combo& combo : combos)
            {
                const auto [bindingId, onlyOpaque]
                    = BindTexture(model, combo.Texture, combo.Palette, combo.Recolor);
                map->Add(combo.Texture, combo.Palette, combo.Recolor, bindingId, onlyOpaque);
            }
            _texPalMap.emplace(model->Id, std::move(map));
        }
    }

    std::int32_t Scene::BindGetTexture(const std::shared_ptr<Model>& model,
        std::int32_t textureId, std::int32_t paletteId, std::int32_t recolorId)
    {
        if (Mods::Headless::Active()) return 0;
        auto found = _texPalMap.find(model->Id);
        if (found != _texPalMap.end())
        {
            return found->second->Get(textureId, paletteId, recolorId).BindingId;
        }
        const auto [bindingId, onlyOpaque] = BindTexture(model, textureId, paletteId, recolorId);
        (void)onlyOpaque;
        return bindingId;
    }

    void Scene::FlatColor::Add(ColorRgba pixel)
    {
        const float alpha = pixel.Alpha / 255.0F;
        _red += pixel.Red * alpha;
        _green += pixel.Green * alpha;
        _blue += pixel.Blue * alpha;
        _weight += alpha;
        _plainRed += pixel.Red;
        _plainGreen += pixel.Green;
        _plainBlue += pixel.Blue;
        _count++;
    }

    Vector3 Scene::FlatColor::Result() const
    {
        if (_weight > 0.01F)
        {
            return Vector3(_red / _weight / 255.0F, _green / _weight / 255.0F,
                _blue / _weight / 255.0F);
        }
        if (_count > 0.0F)
        {
            return Vector3(_plainRed / _count / 255.0F, _plainGreen / _count / 255.0F,
                _plainBlue / _count / 255.0F);
        }
        return Vector3(1.0F, 1.0F, 1.0F);
    }

    std::pair<std::int32_t, bool> Scene::BindTexture(const std::shared_ptr<Model>& model,
        std::int32_t textureId, std::int32_t paletteId, std::int32_t recolorId)
    {
        const auto& texture = model->Recolors->at(static_cast<std::size_t>(recolorId))
            ->Textures->at(static_cast<std::size_t>(textureId));
        auto owned = Gpu().CreateTexture(NativeRuntime::Rhi::TextureDesc{
            static_cast<std::uint32_t>(texture.Width), static_cast<std::uint32_t>(texture.Height), 1, 1, 1, 1,
            NativeRuntime::Rhi::TextureFormat::RGBA8Unorm,
            NativeRuntime::Rhi::TextureUsage::Sampled | NativeRuntime::Rhi::TextureUsage::TransferDst});
        const std::int32_t bindingId = owned->Handle().value;
        bool onlyOpaque = true;
        std::vector<std::uint32_t> pixels;
        FlatColor average;
        for (ColorRgba pixel : model->GetPixels(textureId, paletteId, recolorId))
        {
            pixels.push_back(pixel.ToUint());
            onlyOpaque = onlyOpaque && pixel.Alpha == 255;
            average.Add(pixel);
        }
        Gpu().WriteTexture(*owned, NativeRuntime::Rhi::TextureWrite{
            static_cast<std::uint32_t>(texture.Width), static_cast<std::uint32_t>(texture.Height),
            NativeRuntime::Rhi::TextureFormat::RGBA8Unorm, pixels.data()});
        _modelTextureSources.insert_or_assign(bindingId,
            SceneModelTextureSource{model, textureId, paletteId, recolorId});
        _ownedTextures.insert_or_assign(bindingId, std::move(owned));
        _flatColors[bindingId] = average.Result();
        return {bindingId, onlyOpaque};
    }

    Vector3 Scene::AverageOf(const std::vector<ColorRgba>& data)
    {
        FlatColor average;
        for (ColorRgba pixel : data) average.Add(pixel);
        return average.Result();
    }

    std::int32_t Scene::BindGetTexture(const std::vector<ColorRgba>& data, std::int32_t width, std::int32_t height)
    {
        const std::int32_t bindingId = CreateOwnedTexture(width, height,
            NativeRuntime::Rhi::TextureFormat::RGBA8Unorm, data.data());
        _flatColors[bindingId] = AverageOf(data);
        return bindingId;
    }

    void Scene::BindTexture(const std::vector<ColorRgba>& data, std::int32_t width, std::int32_t height,
        std::int32_t bindingId)
    {
        WriteOwnedTexture(bindingId, width, height, NativeRuntime::Rhi::TextureFormat::RGBA8Unorm, data.data());
        _flatColors[bindingId] = AverageOf(data);
    }

    std::int32_t Scene::CreateOwnedTexture(std::int32_t width, std::int32_t height,
        NativeRuntime::Rhi::TextureFormat format, const void* pixels)
    {
        auto owned = Gpu().CreateTexture(NativeRuntime::Rhi::TextureDesc{
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1, 1, 1, 1, format,
            NativeRuntime::Rhi::TextureUsage::Sampled | NativeRuntime::Rhi::TextureUsage::TransferDst});
        const std::int32_t bindingId = owned->Handle().value;
        Gpu().WriteTexture(*owned, NativeRuntime::Rhi::TextureWrite{
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), format, pixels});
        KeepTextureCopy(bindingId, width, height, format, pixels, true);
        _ownedTextures.insert_or_assign(bindingId, std::move(owned));
        return bindingId;
    }

    // Write a texture under a handle that may not be ours: a HUD element
    // rewriting the texture it was given, or a caller with a reserved range
    // of its own (the map thumbnails). A handle nothing holds yet becomes a
    // texture the device keeps, as the GL name it used to be was kept.
    void Scene::WriteOwnedTexture(std::int32_t bindingId, std::int32_t width, std::int32_t height,
        NativeRuntime::Rhi::TextureFormat format, const void* pixels)
    {
        NativeRuntime::Rhi::Texture* texture = Gpu().FindTexture(NativeRuntime::Rhi::TextureHandle{bindingId});
        if (texture == nullptr)
        {
            texture = &Gpu().RetainTexture(Gpu().CreateTexture(NativeRuntime::Rhi::TextureDesc{
                static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1, 1, 1, 1, format,
                NativeRuntime::Rhi::TextureUsage::Sampled | NativeRuntime::Rhi::TextureUsage::TransferDst},
                NativeRuntime::Rhi::TextureHandle{bindingId}));
        }
        Gpu().WriteTexture(*texture, NativeRuntime::Rhi::TextureWrite{
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), format, pixels});
        KeepTextureCopy(bindingId, width, height, format, pixels, _ownedTextures.contains(bindingId));
    }

    void Scene::KeepTextureCopy(std::int32_t bindingId, std::int32_t width, std::int32_t height,
        NativeRuntime::Rhi::TextureFormat format, const void* pixels, bool owned)
    {
        using NativeRuntime::Rhi::TextureFormat;
        std::size_t bytes = 0;
        switch (format)
        {
        case TextureFormat::R8Unorm: bytes = 1; break;
        case TextureFormat::RG8Unorm: bytes = 2; break;
        case TextureFormat::RGB8Unorm: bytes = 3; break;
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA8Srgb:
        case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8Srgb: bytes = 4; break;
        default: return;
        }
        if (pixels == nullptr || width <= 0 || height <= 0) return;
        SceneTextureCopy& copy = _textureCopies[bindingId];
        copy.Width = width;
        copy.Height = height;
        copy.Format = format;
        copy.Owned = owned;
        const auto* begin = static_cast<const std::uint8_t*>(pixels);
        copy.Pixels.assign(begin, begin + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bytes);
    }

    bool Scene::IsRoomModel(const Model* model) const
    {
        if (_room == nullptr || model == nullptr) return false;
        for (const std::shared_ptr<ModelInstance>& inst : _room->GetModels())
        {
            if (inst && inst->Model().get() == model) return true;
        }
        return false;
    }

    void Scene::RebindInput(MphRead::RendererPlatform::KeyboardState& keyboard,
        MphRead::RendererPlatform::MouseState& mouse) noexcept
    {
        _keyboardState = &keyboard;
        _mouseState = &mouse;
    }

    // The renderer is being switched under a running match. Everything this
    // scene holds on the device goes; everything it knows stays -- the
    // texture/palette map and every handle in it, the original models and
    // HUD/dynamic recovery data -- so RebuildGpuAfterSwitch can put it all back
    // on the next device under the same handles.
    void Scene::ReleaseGpuForSwitch()
    {
        if (Mods::Headless::Active()) return;
        _ownedTextures.clear();
        _gpuMeshCache.Clear();
        _transientGeometry.reset();
        _celDepthView.reset();
        _celDepth.reset();
        _celColorView.reset();
        _celColor.reset();
        _sceneDepthStencilView.reset();
        _sceneDepthStencil.reset();
        _sceneColorView.reset();
        _sceneColor.reset();
        for (auto& sampler : _samplers) sampler.reset();
        _commands.reset();
        _pipelines.clear(); _recentPipelines = {};
        _shaderConstants = &_noShaderConstants;
        _sceneShaders.reset();
        auto* device = std::exchange(_gpu, nullptr);
        if (device != nullptr)
        {
            try { device->WaitIdle(); }
            catch (const NativeRuntime::Rhi::BackendError& error)
            {
                // A lost device cannot establish ordinary completion. Its
                // session teardown closes all remaining native ownership.
                if (error.Kind() != NativeRuntime::Rhi::BackendErrorKind::DeviceLost) throw;
            }
        }
    }

    void Scene::RebuildGpuAfterSwitch(const std::function<void()>& checkpoint)
    {
        if (Mods::Headless::Active()) return;
        Commands().Begin();
        InitShaders();
        _transientGeometry = NativeRuntime::Rhi::CreateSceneTransientGeometry(Gpu(), Commands());
        std::size_t textures = 0;
        for (const auto& [bindingId, source] : _modelTextureSources)
        {
            const auto& model = source.Model;
            const auto& texture = model->Recolors->at(static_cast<std::size_t>(source.RecolorId))
                ->Textures->at(static_cast<std::size_t>(source.TextureId));
            auto made = Gpu().CreateTexture(NativeRuntime::Rhi::TextureDesc{
                static_cast<std::uint32_t>(texture.Width), static_cast<std::uint32_t>(texture.Height), 1, 1, 1, 1,
                NativeRuntime::Rhi::TextureFormat::RGBA8Unorm,
                NativeRuntime::Rhi::TextureUsage::Sampled | NativeRuntime::Rhi::TextureUsage::TransferDst},
                NativeRuntime::Rhi::TextureHandle{bindingId});
            std::vector<std::uint32_t> pixels;
            for (ColorRgba pixel : model->GetPixels(source.TextureId, source.PaletteId, source.RecolorId))
                pixels.push_back(pixel.ToUint());
            Gpu().WriteTexture(*made, NativeRuntime::Rhi::TextureWrite{
                static_cast<std::uint32_t>(texture.Width), static_cast<std::uint32_t>(texture.Height),
                NativeRuntime::Rhi::TextureFormat::RGBA8Unorm, pixels.data()});
            _ownedTextures.insert_or_assign(bindingId, std::move(made));
            ++textures;
            if (textures == 1 && checkpoint) checkpoint();
        }
        std::size_t recoveryBytes = 0;
        for (const auto& [bindingId, copy] : _textureCopies)
        {
            const NativeRuntime::Rhi::TextureHandle handle{bindingId};
            NativeRuntime::Rhi::Texture* texture = Gpu().FindTexture(handle);
            if (texture == nullptr)
            {
                auto made = Gpu().CreateTexture(NativeRuntime::Rhi::TextureDesc{
                    static_cast<std::uint32_t>(copy.Width), static_cast<std::uint32_t>(copy.Height), 1, 1, 1, 1,
                    copy.Format,
                    NativeRuntime::Rhi::TextureUsage::Sampled | NativeRuntime::Rhi::TextureUsage::TransferDst},
                    handle);
                texture = made.get();
                if (copy.Owned) _ownedTextures.insert_or_assign(bindingId, std::move(made));
                else texture = &Gpu().RetainTexture(std::move(made));
            }
            Gpu().WriteTexture(*texture, NativeRuntime::Rhi::TextureWrite{
                static_cast<std::uint32_t>(copy.Width), static_cast<std::uint32_t>(copy.Height), copy.Format,
                copy.Pixels.data()});
            ++textures;
            recoveryBytes += copy.Pixels.size();
            if (textures == 1 && checkpoint) checkpoint();
        }
        if (textures == 0 && checkpoint) checkpoint();
        UpdateProjection();
        Mods::DebugLog::Line("render", "the match's GPU side was rebuilt on the new renderer: "
            + std::to_string(textures) + " textures (" + std::to_string(_modelTextureSources.size())
            + " from original model data, " + std::to_string(recoveryBytes)
            + " recovery bytes for HUD/dynamic textures); meshes follow as they are drawn");
    }

    // The device for this scene's GL context, and this scene's command list.
    // Both are there as soon as anything asks: a scene creates textures
    // before OnLoad (a player's models load as the player is added), and
    // the GL calls those used to be needed a context and nothing more.
    NativeRuntime::Rhi::GraphicsDevice& Scene::Gpu()
    {
        if (_gpu == nullptr)
        {
            _gpu = &NativeRuntime::Rhi::SceneDevice();
        }
        return *_gpu;
    }

    NativeRuntime::Rhi::CommandList& Scene::Commands()
    {
        if (!_commands)
        {
            _commands = Gpu().CreateCommandList();
        }
        return *_commands;
    }

    // Each pass's state, as OnRenderFrame's GL calls used to set it one call
    // at a time. The item's culling, fill and line width are laid on top by
    // ScenePipeline; everything else about a pass is here.
    NativeRuntime::Rhi::GraphicsPipelineDesc Scene::DescribeScenePass(ScenePass pass) const
    {
        namespace Rhi = NativeRuntime::Rhi;
        Rhi::GraphicsPipelineDesc desc{};
        desc.rasterizer.cullMode = Rhi::CullMode::None;
        Rhi::DepthStencilStateDesc& ds = desc.depthStencil;
        ds.depthTestEnable = true;
        ds.depthWriteEnable = true;
        ds.depthCompareOp = Rhi::CompareOp::LessEqual;
        ds.stencilReadMask = 0xFF;
        ds.stencilWriteMask = 0xFF;
        Rhi::BlendAttachmentDesc blend{};
        blend.srcColorFactor = Rhi::BlendFactor::SrcAlpha;
        blend.dstColorFactor = Rhi::BlendFactor::OneMinusSrcAlpha;
        blend.srcAlphaFactor = Rhi::BlendFactor::SrcAlpha;
        blend.dstAlphaFactor = Rhi::BlendFactor::OneMinusSrcAlpha;
        blend.writeMask = Rhi::ColorWriteMask::All;
        const auto stencil = [&ds](Rhi::StencilOp fail, Rhi::StencilOp depthFail, Rhi::StencilOp pass2,
            Rhi::CompareOp compare)
        {
            ds.stencilTestEnable = true;
            ds.front = Rhi::StencilFaceStateDesc{fail, depthFail, pass2, compare};
            ds.back = ds.front;
        };
        switch (pass)
        {
        case ScenePass::Opaque:
            // Blending is off: an opaque pass keeps alpha == 1.0 only, which
            // blends to itself whatever the blend state says.
            ds.depthCompareOp = Rhi::CompareOp::Less;
            stencil(Rhi::StencilOp::Zero, Rhi::StencilOp::Zero, Rhi::StencilOp::Zero, Rhi::CompareOp::Always);
            desc.alphaTest = Rhi::AlphaTestMode::EqualOne;
            break;
        case ScenePass::Decal:
            stencil(Rhi::StencilOp::Zero, Rhi::StencilOp::Zero, Rhi::StencilOp::Zero, Rhi::CompareOp::Always);
            desc.rasterizer.depthBiasEnable = true;
            desc.rasterizer.depthBiasConstant = -1.0F;
            desc.rasterizer.depthBiasSlope = -1.0F;
            blend.blendEnable = true;
            break;
        case ScenePass::TranslucentStencil:
            stencil(Rhi::StencilOp::Keep, Rhi::StencilOp::Keep, Rhi::StencilOp::Replace, Rhi::CompareOp::Greater);
            desc.alphaTest = Rhi::AlphaTestMode::LessThanOne;
            blend.blendEnable = true;
            blend.writeMask = Rhi::ColorWriteMask::None;
            break;
        case ScenePass::DepthRebuild:
            stencil(Rhi::StencilOp::Keep, Rhi::StencilOp::Keep, Rhi::StencilOp::Keep, Rhi::CompareOp::Always);
            desc.alphaTest = Rhi::AlphaTestMode::EqualOne;
            blend.blendEnable = true;
            blend.writeMask = Rhi::ColorWriteMask::None;
            break;
        case ScenePass::TranslucentNotEqual:
        case ScenePass::TranslucentEqual:
            stencil(Rhi::StencilOp::Keep, Rhi::StencilOp::Keep, Rhi::StencilOp::Keep,
                pass == ScenePass::TranslucentEqual ? Rhi::CompareOp::Equal : Rhi::CompareOp::NotEqual);
            desc.alphaTest = Rhi::AlphaTestMode::LessThanOne;
            ds.depthWriteEnable = false;
            blend.blendEnable = true;
            break;
        case ScenePass::TranslucentSingle:
            desc.alphaTest = Rhi::AlphaTestMode::LessThanOne;
            ds.depthWriteEnable = false;
            blend.blendEnable = true;
            break;
        case ScenePass::AfterScene:
            blend.blendEnable = true;
            break;
        case ScenePass::Preview:
            ds.depthCompareOp = Rhi::CompareOp::Less;
            blend.blendEnable = true;
            break;
        case ScenePass::HudModel:
        case ScenePass::Composite:
        case ScenePass::CompositeShift:
            ds.depthTestEnable = false;
            blend.blendEnable = true;
            break;
        case ScenePass::CelOutline:
            ds.depthTestEnable = false;
            break;
        case ScenePass::FrameEnd:
            break;
        }
        // Which program the pass runs. FrameEnd names none and leaves the
        // last one bound, as the end of the frame always did.
        if (_sceneShaders)
        {
            Rhi::SceneProgram program = Rhi::SceneProgram::Main;
            bool bound = true;
            switch (pass)
            {
            case ScenePass::Composite: program = Rhi::SceneProgram::Composite; break;
            case ScenePass::CompositeShift: program = Rhi::SceneProgram::Shift; break;
            case ScenePass::CelOutline: program = Rhi::SceneProgram::CelOutline; break;
            case ScenePass::FrameEnd: bound = false; break;
            default: break;
            }
            if (bound)
            {
                desc.vertexShader = &_sceneShaders->Vertex(program);
                desc.fragmentShader = &_sceneShaders->Fragment(program);
            }
        }
        desc.blendAttachments.push_back(blend);
        desc.colorFormats.push_back(Rhi::TextureFormat::RGB8Unorm);
        desc.depthStencilFormat = NativeRuntime::Rhi::SceneDevice().GetCapabilities().depthStencilFormat;
        return desc;
    }

    const NativeRuntime::Rhi::GraphicsPipeline& Scene::ScenePipeline(ScenePass pass,
        NativeRuntime::Rhi::CullMode cull, NativeRuntime::Rhi::FillMode fill, std::int32_t lineWidth)
    {
        const std::uint32_t key = (static_cast<std::uint32_t>(pass) << 16U)
            | (static_cast<std::uint32_t>(cull) << 12U) | (static_cast<std::uint32_t>(fill) << 8U)
            | (static_cast<std::uint32_t>(lineWidth) & 0xFFU);
        for (const auto& [recentKey, recent] : _recentPipelines)
            if (recent && recentKey == key) return *recent;
        auto& pipeline = _pipelines[key];
        if (!pipeline)
        {
            NativeRuntime::Rhi::GraphicsPipelineDesc desc = DescribeScenePass(pass);
            desc.rasterizer.cullMode = cull;
            desc.rasterizer.fillMode = fill;
            desc.rasterizer.lineWidth = static_cast<float>(lineWidth);
            pipeline = Gpu().CreateGraphicsPipeline(desc);
        }
        _recentPipelines[_recentPipelineNext] = {key, pipeline.get()};
        _recentPipelineNext = (_recentPipelineNext + 1) % _recentPipelines.size();
        return *pipeline;
    }

    // A pass begins with its state applied even when it has no items, as
    // the calls that set it one at a time always were: the depth clear
    // between the stencil and rebuild passes depends on it.
    void Scene::BeginScenePass(ScenePass pass)
    {
        _itemPass = pass;
        Commands().SetPipeline(ScenePipeline(pass, NativeRuntime::Rhi::CullMode::None,
            NativeRuntime::Rhi::FillMode::Solid, 1));
    }

    namespace
    {
        // FRUITY_REBUILD_DEPTH=1 (A/B checks only): draw the opaque depth again
        // as before, instead of copying it aside and back.
        bool SaveOpaqueDepth() noexcept
        {
            static const bool save = std::getenv("FRUITY_REBUILD_DEPTH") == nullptr;
            return save;
        }
    }

    // The scene's depth targets can be copied on Vulkan, where the opaque
    // depth is kept aside instead of drawn again (SaveAttachmentDepth). OpenGL
    // keeps its targets as they were.
    NativeRuntime::Rhi::TextureUsage Scene::SceneDepthCopyUsage()
    {
        namespace Rhi = NativeRuntime::Rhi;
#if defined(__ANDROID__)
        // A tiled GPU would write the depth out to memory for the copy: not
        // measured on a phone, so Android keeps drawing it again.
        return Rhi::TextureUsage{};
#else
        return Rhi::SelectedSceneBackend() == Rhi::GraphicsBackend::Vulkan
            ? Rhi::TextureUsage::TransferSrc | Rhi::TextureUsage::TransferDst : Rhi::TextureUsage{};
#endif
    }

    void Scene::CreateSceneTargets(Vector2i size)
    {
        namespace Rhi = NativeRuntime::Rhi;
        const auto width = static_cast<std::uint32_t>(size.X);
        const auto height = static_cast<std::uint32_t>(size.Y);
        // SceneColor, CelColor and SceneDepthStencil, in the order their
        // names were always taken.
        _sceneColor = Gpu().CreateTexture(Rhi::TextureDesc{width, height, 1, 1, 1, 1,
            Rhi::TextureFormat::RGB8Unorm,
            Rhi::TextureUsage::Sampled | Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSrc});
        _sceneColorView = Gpu().CreateTextureView(*_sceneColor, Rhi::TextureViewDesc{Rhi::TextureFormat::RGB8Unorm});
        _celColor = Gpu().CreateTexture(Rhi::TextureDesc{width, height, 1, 1, 1, 1,
            Rhi::TextureFormat::RGB8Unorm,
            Rhi::TextureUsage::Sampled | Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferDst
                | Rhi::TextureUsage::TransferSrc});
        _celColorView = Gpu().CreateTextureView(*_celColor, Rhi::TextureViewDesc{Rhi::TextureFormat::RGB8Unorm});
        const auto depthStencil = Gpu().GetCapabilities().depthStencilFormat;
        _sceneDepthStencil = Gpu().CreateTexture(Rhi::TextureDesc{width, height, 1, 1, 1, 1,
            depthStencil, Rhi::TextureUsage::DepthStencilAttachment | SceneDepthCopyUsage()});
        _sceneDepthStencilView = Gpu().CreateTextureView(*_sceneDepthStencil,
            Rhi::TextureViewDesc{depthStencil});
    }

    // The scene target: SceneColor, over CelDepth while the cel outline
    // wants a depth it can read and SceneDepthStencil otherwise. Everything
    // is loaded -- the frame's clears are still their own calls.
    NativeRuntime::Rhi::RenderingInfo Scene::SceneRenderingInfo(
        std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1>& color,
        NativeRuntime::Rhi::RenderingDepthStencilAttachment& depth) const
    {
        color[0].view = _sceneColorView.get();
        depth.view = _celDepthView ? _celDepthView.get() : _sceneDepthStencilView.get();
        NativeRuntime::Rhi::RenderingInfo info{};
        info.width = static_cast<std::uint32_t>(_targetSize.X);
        info.height = static_cast<std::uint32_t>(_targetSize.Y);
        info.colorAttachments = color;
        info.depthStencilAttachment = &depth;
        return info;
    }

    void Scene::BeginSceneRendering(NativeRuntime::Rhi::LoadOp colorLoad, NativeRuntime::Rhi::LoadOp depthLoad,
        NativeRuntime::Rhi::LoadOp stencilLoad, NativeRuntime::Rhi::ClearColor clearColor,
        NativeRuntime::Rhi::Scissor area)
    {
        std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
        NativeRuntime::Rhi::RenderingDepthStencilAttachment depth{};
        NativeRuntime::Rhi::RenderingInfo info = SceneRenderingInfo(color, depth);
        color[0].loadOp = colorLoad;
        color[0].clearValue = clearColor;
        depth.depthLoadOp = depthLoad;
        depth.stencilLoadOp = stencilLoad;
        depth.clearDepth = 1.0F;
        depth.clearStencil = 0;
        info.renderArea = area;
        Commands().BeginRendering(info);
    }

    // The colour the scene target is cleared to: the room's, which is
    // black unless it is a First Hunt room whose fog is its sky.
    NativeRuntime::Rhi::ClearColor Scene::SceneClearColor() const
    {
        return NativeRuntime::Rhi::ClearColor{_clearColor.X, _clearColor.Y, _clearColor.Z, _clearColor.W};
    }

    // The cel pass writes SceneColor with no depth attached.
    void Scene::BeginCelRendering()
    {
        std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
        color[0].view = _sceneColorView.get();
        NativeRuntime::Rhi::RenderingInfo info{};
        info.width = static_cast<std::uint32_t>(_targetSize.X);
        info.height = static_cast<std::uint32_t>(_targetSize.Y);
        info.colorAttachments = color;
        Commands().BeginRendering(info);
    }

    void Scene::BeginWindowRendering(NativeRuntime::Rhi::LoadOp colorLoad, NativeRuntime::Rhi::LoadOp depthLoad,
        NativeRuntime::Rhi::ClearColor clearColor, NativeRuntime::Rhi::Scissor area)
    {
        std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
        color[0].loadOp = colorLoad;
        color[0].clearValue = clearColor;
        NativeRuntime::Rhi::RenderingDepthStencilAttachment depth{};
        depth.depthLoadOp = depthLoad;
        NativeRuntime::Rhi::RenderingInfo info{};
        info.width = static_cast<std::uint32_t>(_rendererSize.X);
        info.height = static_cast<std::uint32_t>(_rendererSize.Y);
        info.swapchain = true;
        info.colorAttachments = color;
        info.depthStencilAttachment = &depth;
        info.renderArea = area;
        Commands().BeginRendering(info);
    }

    NativeRuntime::Rhi::Texture* Scene::TextureFor(std::int32_t bindingId) const
    {
        return bindingId != 0 && _gpu != nullptr
            ? _gpu->FindTexture(NativeRuntime::Rhi::TextureHandle{bindingId}) : nullptr;
    }

    const NativeRuntime::Rhi::Sampler& Scene::SamplerFor(bool linear, RepeatMode s, RepeatMode t)
    {
        namespace Rhi = NativeRuntime::Rhi;
        const auto address = [](RepeatMode mode)
        {
            switch (mode)
            {
            case RepeatMode::Clamp: return Rhi::SamplerAddressMode::ClampToEdge;
            case RepeatMode::Mirror: return Rhi::SamplerAddressMode::MirroredRepeat;
            case RepeatMode::Repeat:
            default: return Rhi::SamplerAddressMode::Repeat;
            }
        };
        const std::size_t index = (linear ? 9U : 0U) + static_cast<std::size_t>(s) * 3U
            + static_cast<std::size_t>(t);
        auto& sampler = _samplers.at(index);
        if (!sampler)
        {
            Rhi::SamplerDesc desc{};
            desc.minFilter = linear ? Rhi::Filter::Linear : Rhi::Filter::Nearest;
            desc.magFilter = desc.minFilter;
            desc.mipFilter = Rhi::Filter::Nearest;
            desc.addressU = address(s);
            desc.addressV = address(t);
            sampler = Gpu().CreateSampler(desc);
        }
        return *sampler;
    }

    void Scene::BindSceneTexture(std::uint32_t slot, std::int32_t bindingId,
        const NativeRuntime::Rhi::Sampler& sampler)
    {
        const NativeRuntime::Rhi::Texture* texture = TextureFor(bindingId);
        Commands().BindSampledTexture(slot, texture, texture != nullptr ? &sampler : nullptr);
    }

    void Scene::BindSceneTexture(std::uint32_t slot, const NativeRuntime::Rhi::Texture& texture,
        const NativeRuntime::Rhi::Sampler& sampler)
    {
        Commands().BindSampledTexture(slot, &texture, &sampler);
    }

    void Scene::UnbindSceneTexture(std::uint32_t slot)
    {
        Commands().BindSampledTexture(slot, nullptr, nullptr);
    }

    void Scene::UpdateMaterials(const std::shared_ptr<Model>& model, std::int32_t recolorId)
    {
        if (Mods::Headless::Active())
        {
            return;
        }
        for (const auto& materialPtr : *model->Materials)
        {
            Material& material = *materialPtr;
            const std::int32_t textureId = material.CurrentTextureId;
            if (textureId == -1)
            {
                continue;
            }
            const std::int32_t paletteId = material.CurrentPaletteId;
            TextureMapValue value = _texPalMap.at(model->Id)->Get(textureId, paletteId, recolorId);
            material.TextureBindingId = value.BindingId;
            material.CurrentTextureId = textureId;
            material.CurrentPaletteId = paletteId;
            UpdateMaterial(material, value.OnlyOpaque);
        }
    }

    void Scene::UpdateMaterial(Material& material, bool onlyOpaque)
    {
        if (material.CurrentAlpha < 1.0F) material.RenderMode = RenderMode::Translucent;
        else if (material.RenderMode != RenderMode::Normal && onlyOpaque) material.RenderMode = RenderMode::Normal;
        else if (material.RenderMode == RenderMode::Normal && !onlyOpaque) material.RenderMode = RenderMode::Translucent;
    }

    void Scene::OnUpdateFrame()
    {
        OnSimulationFrame();
        OnDrawFrame();
    }

    void Scene::OnSimulationFrame()
    {
        const std::lock_guard<std::recursive_mutex> gate(NativeRuntime::SceneGate());
        if (Mods::Network::NetSession::FreezeGameplay())
        {
            if (Mods::Network::NetSession::IsStarting())
            {
                Mods::Network::NetSession::MarkMatchLoaded();
            }
            Mods::Network::NetSession::Pump();
            return;
        }
        ++_effectFrame;
        _frameTime = 1.0F / 60.0F;
        if (_breakNextFrame)
        {
            _frameAdvanceOn = true;
            _breakNextFrame = false;
        }
        if (ProcessFrame())
        {
            _globalElapsedTime += _frameTime;
            if (GameState::MatchState() == MatchState::InProgress
                && !GameState::DialogPause() && !GameState::MenuPause())
            {
                _elapsedTime += _frameTime;
            }
            if (_inputMode == InputMode::CameraOnly || Mods::Chat::ChatBox::Composing())
            {
                Entities::PlayerEntity::Main()->Controls().ClearAll();
            }
            else if (Mods::Chat::ChatBox::ConsumeJustClosed())
            {
                Entities::PlayerEntity::Main()->ModForgetInputDeltas();
            }
            Mods::Network::DemoPlayback::PumpFrame();
            Mods::Network::NetSession::Update(_globalElapsedTime);
            if (Mods::Network::NetSession::FreezeGameplay())
            {
                return;
            }
            if (Mods::Network::DemoPlayback::IsActive() && !Mods::SpectatorMode::IsSpectating())
            {
                Mods::SpectatorMode::Start(true);
            }
            if (auto freeCamera = Mods::SpectatorMode::TakeCameraRequest(); freeCamera.has_value())
            {
                SetFreeCamera(freeCamera.value());
            }
            Mods::Input::GamepadDesktop::Poll();
            Mods::Input::GamepadContexts::Current(Mods::Input::GamepadContexts::Resolve(
                Mods::Chat::ChatBox::Composing(), Mods::EndScreen::Available()));
            Mods::Input::GamepadInput::BeginFrame();
            if (Mods::SpectatorMode::IsSpectating() && !Mods::PauseMenu::Open())
            {
                const Mods::Input::SpectatorInput spectator = Mods::Input::SpectatorInput::ReadController();
                spectator.ApplyView();
                Mods::SpectatorMode::NoteScoreboard(
                    _keyboardState->IsKeyDown(RendererPlatform::Key::Tab) || spectator.Scoreboard());
                // Rates per 60 Hz step; MoveRoamCamera applies them per drawn frame.
                _roamPadMoveX = _freeCam ? spectator.MoveX() : 0.0F;
                _roamPadMoveY = _freeCam ? spectator.MoveY() : 0.0F;
                _roamPadRise = _freeCam ? spectator.Ascend() - spectator.Descend() : 0.0F;
                _roamPadLookX = _freeCam ? spectator.LookX() : 0.0F;
                _roamPadLookY = _freeCam ? spectator.LookY() : 0.0F;
            }
            else
            {
                _roamPadMoveX = _roamPadMoveY = _roamPadRise = _roamPadLookX = _roamPadLookY = 0.0F;
            }
            Mods::EndScreen::PollGamepad();
            const bool noPlayerInput = _inputMode == InputMode::CameraOnly
                || Mods::PauseMenu::Open() || Mods::Chat::ChatBox::Composing()
                || (!Mods::Headless::Active() && !Mods::Input::GamepadContexts::Focused());
            Entities::PlayerEntity::ProcessInput(*_keyboardState, *_mouseState, noPlayerInput);
            if (!noPlayerInput && !Mods::SpectatorMode::IsSpectating())
            {
                Mods::Input::GamepadInput::Apply(Entities::PlayerEntity::Main().get());
            }
            Mods::Network::NetHooks::AfterInput(*this);
            if (_room) _room->UpdateTransition();
        }
        OnKeyHeld();
        MoveRoamCamera();
        if (ProcessFrame() && _room)
        {
            GameState::ProcessFrame(this);
            ModStepPreview();
            if (GameState::MatchState() == MatchState::InProgress && !GameState::MenuPause())
            {
                UpdateScene();
            }
            Mods::Network::NetHitClaims::Tick();
            Mods::Network::NetHooks::AfterSimulation();
            Mods::Network::NetHitPrediction::Tick();
            if (!Mods::Headless::Active())
            {
                if (!GameState::MenuPause()) Sound::Sfx::Update(_frameTime);
                Music::UpdateMusic();
            }
        }
        auto main = Entities::PlayerEntity::Main();
        if (ProcessFrame() && !Mods::Headless::Active()
            && ((main->LoadFlags() & LoadFlags::Active) == LoadFlags::Active))
        {
            main->UpdateHud();
        }
        if (ProcessFrame())
        {
            if (GameState::MatchState() == MatchState::InProgress
                && !GameState::DialogPause() && !GameState::MenuPause())
            {
                ProcessMessageQueue();
                ++_liveFrames;
            }
            if (!GameState::DialogPause() && !GameState::MenuPause())
            {
                ++_frameCount;
            }
            GameState::UpdateTime(this);
            if (_movieFrameIndex != -1) UpdateMovie();
        }
        _frameAdvanceLastFrame = _frameAdvanceOn;
        _pendingEffectSteps = std::min(_pendingEffectSteps + 1, Mods::Render::FrameTiming::MaxCatchUpSteps);
        _pendingFadeSteps = std::min(_pendingFadeSteps + 1, Mods::Render::FrameTiming::MaxCatchUpSteps);
        if (Mods::Headless::Active()) ModStepDrawPassTimers();
    }

    void Scene::ModStepDrawPassTimers()
    {
        if (ProcessFrame() && GameState::MatchState() == MatchState::InProgress && !GameState::DialogPause())
        {
            for (std::int32_t i = 0; i < _pendingEffectSteps; ++i)
            {
                const std::uint64_t owed = static_cast<std::uint64_t>(_pendingEffectSteps - 1 - i);
                ProcessEffects(_effectFrame >= owed ? _effectFrame - owed : _effectFrame);
            }
        }
        _pendingEffectSteps = 0;
        if (ProcessFrame()) UpdateFade();
    }

    void Scene::OnDrawFrame()
    {
        const std::lock_guard<std::recursive_mutex> gate(NativeRuntime::SceneGate());
        Mods::EndScreen::Tick(_room != nullptr ? _room->Meta().Name : std::string(), _globalElapsedTime);
        Mods::Render::MapThumbnail::BeginFrame();
        (void)Gpu().BeginFrame();
        // Fade completion can synchronously load a room and release the old
        // model. Resolve it before collecting draws that reference that model.
        // UpdateUniforms still refreshes fade state injected after collection
        // by capture tools, but ordinary room transitions finish here.
        if (ProcessFrame()) UpdateFade();
        Vector2i target = RenderSize();
        if (target != _targetSize)
        {
            OnResize();
            target = _targetSize;
        }
        UpdateDepthAttachment(target);
        BeginSceneRendering();
        Commands().SetViewport(NativeRuntime::Rhi::Viewport{0.0F, 0.0F,
            static_cast<float>(target.X), static_cast<float>(target.Y)});
        // The main program and the state the HUD and preview start from: what
        // the frame's first uniforms (UpdateUniforms) are written into.
        Commands().SetPipeline(ScenePipeline(ScenePass::AfterScene, NativeRuntime::Rhi::CullMode::None,
            NativeRuntime::Rhi::FillMode::Solid, 1));
        LoadAndUnload();
        _decalItems.clear();
        _nonDecalItems.clear();
        _translucentItems.clear();
        while (!_usedRenderItems.empty())
        {
            auto item = std::move(_usedRenderItems.front());
            _usedRenderItems.pop_front();
            if (item->Type != RenderItemType::Mesh)
            {
                NativeRuntime::ReturnToSharedArrayPool(item->Points);
            }
            _freeRenderItems.push(std::move(item));
        }
        _nextPolygonId = 1;
        _singleParticleCount = 0;
        if (ProcessFrame() || CameraMode() != MphRead::CameraMode::Player)
        {
            TransformCamera();
            UpdateCameraPosition();
        }
        UpdateProjection();
        GetDrawItems();
    }

    Matrix4 Scene::GetPerspectiveMatrix(float fov) const
    {
        const float aspect = static_cast<float>(_rendererSize.X) / static_cast<float>(_rendererSize.Y);
        return Matrix4::CreatePerspectiveFieldOfView(fov, aspect, _nearClip, _useClip ? _farClip : 10000.0F);
    }

    void Scene::UpdateProjection()
    {
        _perspectiveMatrix = GetPerspectiveMatrix(_cameraFov);
        _shaderConstants->SetProjection(_perspectiveMatrix);
        auto main = Entities::PlayerEntity::Main();
        const Vector3 camPos = RequireReference(main->CameraInfo()).Position;
        const Vector3 camRight(_viewMatrix.M11, _viewMatrix.M12, -_viewMatrix.M13);
        const Vector3 camUp(_viewMatrix.M21, _viewMatrix.M22, -_viewMatrix.M23);
        const Vector3 camFacing(_viewMatrix.M31, _viewMatrix.M32, -_viewMatrix.M33);
        const auto computePlane = [&](Vector3 input)
        {
            Vector3 normal(Vector3::Dot(input, camRight), Vector3::Dot(input, camUp), Vector3::Dot(input, camFacing));
            return Vector4(normal, Vector3::Dot(normal, camPos));
        };
        const float aspect = static_cast<float>(_rendererSize.X) / static_cast<float>(_rendererSize.Y);
        const float cosFov = std::cos(_cameraFov / 2.0F);
        const float cosFovDiv = cosFov / aspect;
        const float sinFov = std::sin(_cameraFov / 2.0F);
        _frustumInfo->Index = 1;
        _frustumInfo->Count = 5;
        (*_frustumInfo->Planes)[0] = SetBoundsIndices([&]() { auto p = computePlane(Vector3(0, 0, 1)); p.W += _nearClip; return p; }());
        (*_frustumInfo->Planes)[1] = SetBoundsIndices(computePlane(Vector3(cosFovDiv, 0, sinFov).Normalized()));
        (*_frustumInfo->Planes)[2] = SetBoundsIndices(computePlane(Vector3(-cosFovDiv, 0, sinFov).Normalized()));
        (*_frustumInfo->Planes)[3] = SetBoundsIndices(computePlane(Vector3(0, -cosFov, sinFov).Normalized()));
        (*_frustumInfo->Planes)[4] = SetBoundsIndices(computePlane(Vector3(0, cosFov, sinFov).Normalized()));
    }

    Formats::Culling::FrustumPlane Scene::SetBoundsIndices(Vector4 plane)
    {
        std::int32_t x1 = 0, x2 = 3;
        if (plane.X < 0) std::swap(x1, x2);
        std::int32_t y1 = 1, y2 = 4;
        if (plane.Y < 0) std::swap(y1, y2);
        std::int32_t z1 = 2, z2 = 5;
        if (plane.Z < 0) std::swap(z1, z2);
        Formats::Culling::FrustumPlane result{};
        result.Plane = plane;
        result.XIndex1 = x1; result.XIndex2 = x2;
        result.YIndex1 = y1; result.YIndex2 = y2;
        result.ZIndex1 = z1; result.ZIndex2 = z2;
        return result;
    }

    std::int32_t Scene::FramebufferStatus() const noexcept
    {
        return _framebufferStatus;
    }

    std::int32_t Scene::DrainGlError()
    {
        return Gpu().DrainErrors();
    }

    std::optional<std::vector<std::uint8_t>> Scene::ReadWindowBuffer(std::int32_t& width, std::int32_t& height)
    {
        width = _rendererSize.X;
        height = _rendererSize.Y;
        if (width <= 0 || height <= 0) return std::nullopt;
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
        NativeRuntime::Rhi::RenderingInfo info{};
        info.width = static_cast<std::uint32_t>(width);
        info.height = static_cast<std::uint32_t>(height);
        info.swapchain = true;
        Commands().ReadColor(info, 0, 0, info.width, info.height,
            NativeRuntime::Rhi::TextureFormat::RGB8Unorm, buffer.data());
        return buffer;
    }

    std::optional<std::vector<std::uint8_t>> Scene::ReadSceneTarget(std::int32_t& width, std::int32_t& height)
    {
        width = _targetSize.X;
        height = _targetSize.Y;
        if (!_sceneColor) return std::nullopt;
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
        std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
        NativeRuntime::Rhi::RenderingDepthStencilAttachment depth{};
        NativeRuntime::Rhi::RenderingInfo info = SceneRenderingInfo(color, depth);
        // The finished picture, outline and all (DrawCelOutline).
        if (_celOutlined) color[0].view = _celColorView.get();
        Commands().ReadColor(info, 0, 0,
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
            NativeRuntime::Rhi::TextureFormat::RGB8Unorm, buffer.data());
        return buffer;
    }

    void Scene::AfterRenderFrame()
    {
        Images::PollReadbacks();
        if (_recording)
        {
            std::ostringstream name;
            name << "frame" << std::setw(4) << std::setfill('0') << _framesRecorded;
            Images::Record(Commands(), _rendererSize.X, _rendererSize.Y, name.str());
            ++_framesRecorded;
        }
        _advanceOneFrame = false;
    }

    void Scene::UpdateDepthAttachment(Vector2i target)
    {
        const bool want = !_depthTextureRefused && Mods::RenderOptions::CelShading()
            && Mods::RenderOptions::CelEdge() > 0.0F;
        if (want == (_celDepth != nullptr)) return;
        if (!want)
        {
            // The scene target falls back to SceneDepthStencil by itself.
            _celDepthView.reset();
            _celDepth.reset();
            return;
        }
        namespace Rhi = NativeRuntime::Rhi;
        _celDepth = Gpu().CreateTexture(Rhi::TextureDesc{
            static_cast<std::uint32_t>(target.X), static_cast<std::uint32_t>(target.Y), 1, 1, 1, 1,
            Gpu().GetCapabilities().depthStencilFormat,
            Rhi::TextureUsage::Sampled | Rhi::TextureUsage::DepthStencilAttachment | SceneDepthCopyUsage()});
        _celDepthView = Gpu().CreateTextureView(*_celDepth, Rhi::TextureViewDesc{Gpu().GetCapabilities().depthStencilFormat});
        _claimedQuantum = MeasureDepthQuantum();
        _depthQuantum = _claimedQuantum;
        std::array<Rhi::RenderingColorAttachment, 1> color{};
        Rhi::RenderingDepthStencilAttachment depth{};
        if (!Gpu().CanRender(SceneRenderingInfo(color, depth)))
        {
            std::cout << "[render] this driver will not read the scene's depth back ("
                << FramebufferErrorText(0x8CDD)
                << "); cel shading keeps its banding and goes without the outline.\n";
            _depthTextureRefused = true;
            _celDepthView.reset();
            _celDepth.reset();
        }
    }

    float Scene::MeasureDepthQuantum()
    {
        constexpr std::int32_t requested = 24;
        std::int32_t bits = requested;
        bool answered = false;
        {
            std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
            NativeRuntime::Rhi::RenderingDepthStencilAttachment depth{};
            const std::uint32_t answer = Gpu().DepthBits(SceneRenderingInfo(color, depth));
            if (answer != 0)
            {
                bits = static_cast<std::int32_t>(answer);
                answered = true;
            }
        }
        if (!_saidDepthSize)
        {
            _saidDepthSize = true;
            if (answered)
                std::cout << "[render] cel outline: the depth buffer is " << bits << " bits\n";
            else
                std::cout << "[render] cel outline: the driver would not say how deep the depth buffer is; assuming the "
                    << requested << " that were asked for\n";
        }
        return 1.0F / (std::pow(2.0F, static_cast<float>(bits)) - 1.0F);
    }

    void Scene::DrawCelOutline()
    {
        if (!Mods::RenderOptions::CelShading() || Mods::RenderOptions::CelEdge() <= 0.0F
            || !_celColor || !_sceneShaders || !_celDepth) return;
        const Vector2i target = _targetSize;
        if (_calibrateInk)
        {
            // Once: the measurement reads the scene target back, so this frame
            // copies the picture aside and draws the outline over it in place.
            Commands().CopyColorAttachmentToTexture(*_celColor,
                static_cast<std::uint32_t>(target.X), static_cast<std::uint32_t>(target.Y));
            _calibrateInk = false;
            CalibrateInk(target);
            DrawCelQuad(target, false);
            return;
        }
        // Every other frame reads SceneColor where it is and draws the
        // outlined picture into CelColor, which the composite then shows:
        // nothing draws into the scene target after this. A copy of the whole
        // target and a pass that loads what it then overwrites, the way this
        // was, are most of a tiled GPU's memory traffic for the frame.
        const auto& nearestClamp = SamplerFor(false, RepeatMode::Clamp, RepeatMode::Clamp);
        BindSceneTexture(1, *_celDepth, nearestClamp);
        BindSceneTexture(0, *_sceneColor, nearestClamp);
        Commands().EndRendering();
        {
            std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
            color[0].view = _celColorView.get();
            // Every pixel is written: nothing of what was there is wanted.
            color[0].loadOp = NativeRuntime::Rhi::LoadOp::Clear;
            NativeRuntime::Rhi::RenderingInfo info{};
            info.width = static_cast<std::uint32_t>(target.X);
            info.height = static_cast<std::uint32_t>(target.Y);
            info.colorAttachments = color;
            Commands().BeginRendering(info);
        }
        DrawCelPicture(target, false);
        UnbindSceneTexture(0);
        UnbindSceneTexture(1);
        Commands().EndRendering();
        _celOutlined = true;
        BeginSceneRendering();
    }

    void Scene::DrawCelQuad(Vector2i target, bool probe)
    {
        const auto& nearestClamp = SamplerFor(false, RepeatMode::Clamp, RepeatMode::Clamp);
        BindSceneTexture(1, *_celDepth, nearestClamp);
        BindSceneTexture(0, *_celColor, nearestClamp);
        Commands().EndRendering();
        BeginCelRendering();
        DrawCelPicture(target, probe);
        UnbindSceneTexture(0);
        UnbindSceneTexture(1);
        Commands().EndRendering();
        BeginSceneRendering();
    }

    // The outline pass's quad, into whatever rendering is open, from the
    // colour and depth bound on units 0 and 1.
    void Scene::DrawCelPicture(Vector2i target, bool probe)
    {
        Commands().SetPipeline(ScenePipeline(ScenePass::CelOutline, NativeRuntime::Rhi::CullMode::None,
            NativeRuntime::Rhi::FillMode::Solid, 1));
        _shaderConstants->Set(NativeRuntime::Rhi::CelPostConstants{
            1.0F / target.X, 1.0F / target.Y, Mods::RenderOptions::CelEdge(), _nearClip,
            _useClip ? _farClip : 10000.0F, _depthQuantum, probe});
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientTexCoord3(1.0F, 1.0F, 0.0F); TransientVertex3(1.0F, 1.0F, 0.0F);
        TransientTexCoord3(0.0F, 1.0F, 0.0F); TransientVertex3(-1.0F, 1.0F, 0.0F);
        TransientTexCoord3(1.0F, 0.0F, 0.0F); TransientVertex3(1.0F, -1.0F, 0.0F);
        TransientTexCoord3(0.0F, 0.0F, 0.0F); TransientVertex3(-1.0F, -1.0F, 0.0F);
        EndTransient();
    }

    void Scene::CalibrateInk(Vector2i target)
    {
        const std::int32_t side = std::min(384, std::min(target.X, target.Y));
        if (side < 32) return;
        const std::int32_t x = (target.X - side) / 2;
        const std::int32_t y = (target.Y - side) / 2;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4U);
        try
        {
            DrawCelQuad(target, true);
            std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
            NativeRuntime::Rhi::RenderingDepthStencilAttachment depth{};
            Commands().ReadColor(SceneRenderingInfo(color, depth), static_cast<std::uint32_t>(x),
                static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(side), static_cast<std::uint32_t>(side),
                NativeRuntime::Rhi::TextureFormat::RGBA8Unorm, pixels.data());
        }
        catch (const std::exception& ex)
        {
            std::cout << "[render] cel outline: could not measure the depth (" << ex.what()
                << "); keeping the fixed threshold\n";
            return;
        }
        std::vector<std::uint8_t> drawn;
        drawn.reserve(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) / 4U);
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            if (pixels[i] != 0) drawn.push_back(pixels[i]);
        if (drawn.size() < static_cast<std::size_t>(side) * static_cast<std::size_t>(side) / 8U)
        {
            _calibrateInk = true;
            return;
        }
        std::vector<std::uint8_t> kinks;
        kinks.reserve(drawn.size() / 8U);
        for (auto value : drawn) if (value > 1) kinks.push_back(value);
        if (kinks.size() < 64)
        {
            std::cout << "[render] cel outline: the depth arrives intact here; keeping the buffer's own step.\n";
            return;
        }
        std::sort(kinks.begin(), kinks.end());
        const std::uint8_t median = kinks[kinks.size() / 2U];
        const float measured = std::pow(2.0F, (median / 255.0F - 1.0F) * 32.0F);
        _depthQuantum = ::MphRead::NativeRuntime::MathMax(_claimedQuantum, measured);
        const float ratio = measured / _claimedQuantum;
        const char* howBad = ratio < 2.0F ? "which is what the buffer stores"
            : ratio < 64.0F ? "which is coarser than it stores, and the ink threshold rises to match"
            : "which is far coarser than it stores; only strong creases and silhouettes will ink";
        std::cout << "[render] cel outline: a flat surface's depth is off by "
            << NativeRuntime::ToStringInvariant(measured, "0.#######e+0") << " here, "
            << NativeRuntime::ToStringInvariant(ratio, "0.#") << "x the "
            << NativeRuntime::ToStringInvariant(_claimedQuantum, "0.#######e+0")
            << " the driver stores -- " << howBad << ".\n";
    }

    float Scene::FramesPerSecond() const noexcept { return _framesPerSecond; }

    void Scene::CountFrame()
    {
        ++_fpsFrames;
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - _fpsClock).count();
        if (elapsed >= 0.5)
        {
            _framesPerSecond = static_cast<float>(_fpsFrames / elapsed);
            _fpsFrames = 0;
            _fpsClock = std::chrono::steady_clock::now();
        }
    }

    bool Scene::OnRenderFrame()
    {
        const NativeRuntime::FrameTelemetry::Scope measured(NativeRuntime::FrameTelemetry::Phase::SceneRender);
        const std::lock_guard<std::recursive_mutex> gate(NativeRuntime::SceneGate());
        CountFrame();
        if (_exiting) return false;
        std::unique_ptr<NativeRuntime::Rhi::TimestampQuerySet> gpuSample;
        if (!SideScene()) gpuSample = Mods::Diagnostics::FramePerformance::BeginGpu(Gpu(), Commands());
        if (_transientGeometry)
        {
            _transientGeometry->BeginFrame();
        }
        _celOutlined = false;
        {
            using NativeRuntime::Rhi::LoadOp;
            BeginSceneRendering(LoadOp::Clear, LoadOp::Clear, LoadOp::Clear, SceneClearColor());
        }
        UpdateUniforms();
        SetPauseMenuUniforms();
        BeginScenePass(ScenePass::Opaque);
        for (const auto& item : _nonDecalItems) RenderItem(item);
        // The depth the rebuild pass below would draw again, kept aside.
        // Map thumbnails are always drawn in performance mode, whatever the
        // player chose, so a preview looks the same on every machine.
        const bool performance = Mods::RenderOptions::PerformanceMode() || Mods::ThumbnailMode::Active();
        const bool opaqueDepthSaved = !performance && SaveOpaqueDepth()
            && Commands().SaveAttachmentDepth();
        BeginScenePass(ScenePass::Decal);
        for (const auto& item : _decalItems) RenderItem(item);
        if (performance)
        {
            // Performance mode: each translucent item once, blended over the
            // opaque depth. No stencil pass, depth clear or depth rebuild.
            BeginScenePass(ScenePass::TranslucentSingle);
            for (const auto& item : _translucentItems) RenderItem(item);
        }
        else
        {
            BeginScenePass(ScenePass::TranslucentStencil);
            for (const auto& item : _translucentItems)
            {
                Commands().SetStencilReference(static_cast<std::uint32_t>(item->PolygonId)); RenderItem(item);
            }
            if (opaqueDepthSaved && Commands().RestoreAttachmentDepth())
            {
                // The opaque depth put back, the stencil kept: what the depth
                // clear and the rebuild pass make, without drawing it again.
            }
            else
            {
                Commands().EndRendering();
                {
                    // Colour and the stencil the translucent pass just wrote are kept.
                    using NativeRuntime::Rhi::LoadOp;
                    BeginSceneRendering(LoadOp::Load, LoadOp::Clear, LoadOp::Load);
                }
                BeginScenePass(ScenePass::DepthRebuild);
                for (const auto& item : _nonDecalItems) RenderItem(item);
            }
            BeginScenePass(ScenePass::TranslucentNotEqual);
            for (const auto& item : _translucentItems)
            {
                Commands().SetStencilReference(static_cast<std::uint32_t>(item->PolygonId)); RenderItem(item);
            }
            BeginScenePass(ScenePass::TranslucentEqual);
            for (const auto& item : _translucentItems)
            {
                Commands().SetStencilReference(static_cast<std::uint32_t>(item->PolygonId)); RenderItem(item);
            }
        }
        BeginScenePass(ScenePass::AfterScene);
        ModDrawPreview();
        auto main = Entities::PlayerEntity::Main();
        if (((main->LoadFlags() & LoadFlags::Active) == LoadFlags::Active) && CameraMode() == MphRead::CameraMode::Player)
        {
            SetHudLayerUniforms(); main->DrawHudModels(); UnsetHudLayerUniforms();
        }
        else if (ScoreboardOverFreeCamera())
        {
            SetHudLayerUniforms(); main->DrawHudModels(); UnsetHudLayerUniforms();
        }
        DrawCelOutline();
        const auto& composite = ScenePipeline(ScenePass::Composite, NativeRuntime::Rhi::CullMode::None,
            NativeRuntime::Rhi::FillMode::Solid, 1);
        Commands().SetPipeline(composite);
        _shaderConstants->SetLayerAlpha(1.0F); _shaderConstants->SetFadeColor(Vector4{});
        if (main->HudDisruptedState() != 0 || main->HudWhiteoutState() != -1)
        {
            const float div = _elapsedTime / (1.0F / 30.0F);
            const std::int32_t index = ::MphRead::NativeRuntime::ConvertToInt32Net9(div);
            const float factor = std::fmod(div, 1.0F);
            Commands().SetPipeline(ScenePipeline(ScenePass::CompositeShift, NativeRuntime::Rhi::CullMode::None,
                NativeRuntime::Rhi::FillMode::Solid, 1));
            _shaderConstants->Set(NativeRuntime::Rhi::DisruptionPostConstants{
                main->HudDisruptionFactor(), index, factor, main->HudWhiteoutFactor()});
            if (main->HudWhiteoutFactor() != 0.0F)
                _shaderConstants->SetWhiteoutTable(Entities::PlayerEntity::HudWhiteoutTable);
        }
        Commands().EndRendering();
        BeginWindowRendering(NativeRuntime::Rhi::LoadOp::Clear, NativeRuntime::Rhi::LoadOp::Load, SceneClearColor());
        Commands().SetViewport(NativeRuntime::Rhi::Viewport{0.0F, 0.0F,
            static_cast<float>(_rendererSize.X), static_cast<float>(_rendererSize.Y)});
        BindSceneTexture(0, _celOutlined ? *_celColor : *_sceneColor, SamplerFor(Mods::RenderOptions::ResolutionScale() < 100,
            RepeatMode::Repeat, RepeatMode::Repeat));
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientTexCoord3(1,1,0); TransientVertex3(1,1,0); TransientTexCoord3(0,1,0); TransientVertex3(-1,1,0);
        TransientTexCoord3(1,0,0); TransientVertex3(1,-1,0); TransientTexCoord3(0,0,0); TransientVertex3(-1,-1,0); EndTransient();
        UnbindSceneTexture(0);
        if (main->HudDisruptedState() != 0 || main->HudWhiteoutState() != -1) Commands().SetPipeline(composite);
        _shaderConstants->SetFadeColor(Vector4(_fadeColor, _fadeColor, _fadeColor, 0.0F));
        if (((main->LoadFlags() & LoadFlags::Active) == LoadFlags::Active) && CameraMode() == MphRead::CameraMode::Player)
        {
            if (GameState::MenuPause()) main->DrawPauseMenuBackground();
            DrawHudLayer(_layer4Info); DrawHudLayer(_layer3Info); DrawHudLayer(_layer1Info); DrawHudLayer(_layer2Info); DrawHudLayer(_layer5Info);
            if (_layer1Info->MaskId != -1)
            {
                // The mask is layer 1's own texture, drawn just above with this sampler.
                BindSceneTexture(1, _layer1Info->MaskId, SamplerFor(false, RepeatMode::Clamp, RepeatMode::Clamp));
                _shaderConstants->SetViewSize(
                    static_cast<float>(_rendererSize.X), static_cast<float>(_rendererSize.Y));
            }
            main->DrawHudObjects(); _shaderConstants->SetUseMask(false);
            if (_layer1Info->MaskId != -1)
            {
                UnbindSceneTexture(1);
            }
            if (GameState::MenuPause()) main->DrawPauseMenuForeground();
        }
        else if (ScoreboardOverFreeCamera()) main->DrawHudObjects();
        Mods::Input::AimAssist::AimAssistDebug::Draw(*this);
        if (_movieFrameIndex != -1) DrawMovieFrame();
        if (((main->LoadFlags() & LoadFlags::Active) == LoadFlags::Active) && CameraMode() == MphRead::CameraMode::Player && _fadeType != MphRead::FadeType::None)
        {
            float percent = _fadeIn ? 1.0F - _fadePercent : _fadePercent;
            if (percent > 0.0F)
            {
                ModGoldenObserveFadeDraw(
                    static_cast<std::int32_t>(_fadeType),
                    _fadeColor,
                    percent);
                _shaderConstants->SetFadeColor(Vector4(_fadeColor, _fadeColor, _fadeColor, percent));
                BeginTransient(TransientPrimitiveTopology::TriangleStrip);
                TransientTexCoord3(1,1,0); TransientVertex3(1,1,0); TransientTexCoord3(0,1,0); TransientVertex3(-1,1,0);
                TransientTexCoord3(1,0,0); TransientVertex3(1,-1,0); TransientTexCoord3(0,0,0); TransientVertex3(-1,-1,0); EndTransient();
            }
        }
        Commands().SetPipeline(ScenePipeline(ScenePass::FrameEnd,
            _faceCulling ? NativeRuntime::Rhi::CullMode::Back : NativeRuntime::Rhi::CullMode::None,
            NativeRuntime::Rhi::FillMode::Solid, 1));
        Mods::Diagnostics::FramePerformance::EndGpu(std::move(gpuSample), Commands());
        Gpu().EndFrame();
        return true;
    }

    void Scene::LoadAndUnload()
    {
        if (_loadQueue.Count() > 0)
        {
            std::tuple<std::string, std::int32_t, bool> item;
            while (_loadQueue.TryDequeue(item))
            {
                try
                {
                    auto entity = AddModel(std::get<0>(item), std::get<1>(item), std::get<2>(item));
                    entity->Initialize();
                }
                catch (const ProgramException&)
                {
                }
            }
        }
        if (_unloadQueue.Count() > 0)
        {
            Selection::Clear();
            std::shared_ptr<Entities::EntityBase> entity;
            while (_unloadQueue.TryDequeue(entity))
            {
                UnloadEntity(entity);
            }
        }
    }

    void Scene::UnloadEntity(const std::shared_ptr<Entities::EntityBase>& entity)
    {
        if (entity->Type == EntityType::Room)
        {
            return;
        }
        entity->Destroy();
        RemoveEntity(entity);
        for (const auto& inst : entity->GetModels())
        {
            const auto model = inst->Model();
            if (Metadata::PreloadResources.contains(model->Name))
            {
                continue;
            }
            bool stillUsed = false;
            for (auto enumerator = Entities().GetEnumerator(); enumerator.MoveNext();)
            {
                for (const auto& otherInst : enumerator.Current()->GetModels())
                {
                    if (otherInst->Model() == model)
                    {
                        stillUsed = true;
                        break;
                    }
                }
                if (stillUsed)
                {
                    break;
                }
            }
            if (!stillUsed)
            {
                UnloadModel(model);
            }
        }
    }

    void Scene::UnloadModel(const std::shared_ptr<Model>& model)
    {
        UnloadModel(model, true);
    }

    void Scene::UnloadModel(const std::shared_ptr<Model>& model, bool removeReadCache)
    {
        if (!Mods::Headless::Active())
        {
            auto mapIt = _texPalMap.find(model->Id);
            if (mapIt != _texPalMap.end())
            {
                for (const auto& [key, value] : mapIt->second->_items)
                {
                    (void)key;
                    _ownedTextures.erase(value.BindingId);
                    _textureCopies.erase(value.BindingId);
                    _modelTextureSources.erase(value.BindingId);
                    _flatColors.erase(value.BindingId);
                }
                _texPalMap.erase(mapIt);
            }
            _gpuMeshCache.EraseModel(model.get());
        }
        if (removeReadCache)
        {
            Read::RemoveModel(model->Name, model->FirstHunt);
        }
    }

    void Scene::TransformCamera()
    {
        _viewMatrix = RendererDetail::IdentityMatrix();
        _viewInvRotMatrix = RendererDetail::IdentityMatrix();
        _viewInvRotYMatrix = RendererDetail::IdentityMatrix();
        if (_cameraMode == MphRead::CameraMode::Pivot)
        {
            _viewMatrix.M43 = -_pivotDistance;
            _viewMatrix = Multiply(CreateRotationX(DegreesToRadians(_pivotAngleX)), _viewMatrix);
            _viewMatrix = Multiply(CreateRotationY(DegreesToRadians(_pivotAngleY)), _viewMatrix);
            _viewInvRotMatrix = _viewInvRotYMatrix = CreateRotationY(DegreesToRadians(-_pivotAngleY));
            _viewInvRotMatrix = Multiply(CreateRotationX(DegreesToRadians(-_pivotAngleX)), _viewInvRotMatrix);
        }
        else if (_cameraMode == MphRead::CameraMode::Roam || _cameraMode == MphRead::CameraMode::Player)
        {
            if (_cameraMode == MphRead::CameraMode::Player)
            {
                const auto main = Entities::PlayerEntity::Main();
                _viewMatrix = RequireReference(main->CameraInfo()).ViewMatrix;
                float fov = RequireReference(main->CameraInfo()).Fov > 0.0F
                    ? RequireReference(main->CameraInfo()).Fov
                    : static_cast<float>(Mods::RenderOptions::DefaultFov);
                fov = std::clamp(fov * Mods::RenderOptions::FovScale(), 1.0F, 175.0F);
                _cameraFov = DegreesToRadians(fov);
            }
            else
            {
                _viewMatrix = Matrix4::LookAt(_cameraPosition, _cameraPosition + _cameraFacing, _cameraUp);
            }
            _viewInvRotMatrix = Matrix4::Transpose(_viewMatrix.ClearTranslation());
            if (_viewInvRotMatrix.M11 != 0.0F || _viewInvRotMatrix.M13 != 0.0F)
            {
                const Vector3 row0 = Vector3(_viewInvRotMatrix.M11, 0.0F, _viewInvRotMatrix.M13).Normalized();
                const Vector3 row2 = Vector3(_viewInvRotMatrix.M31, 0.0F, _viewInvRotMatrix.M33).Normalized();
                _viewInvRotYMatrix.M11 = row0.X;
                _viewInvRotYMatrix.M12 = row0.Y;
                _viewInvRotYMatrix.M13 = row0.Z;
                _viewInvRotYMatrix.M31 = row2.X;
                _viewInvRotYMatrix.M32 = row2.Y;
                _viewInvRotYMatrix.M33 = row2.Z;
            }
        }
        _shaderConstants->SetView(_viewMatrix);
    }

    void Scene::UpdateCameraPosition()
    {
        if (_cameraMode == MphRead::CameraMode::Pivot)
        {
            float angleY = _pivotAngleY + 90.0F;
            if (angleY > 360.0F)
            {
                angleY -= 360.0F;
            }
            float angleX = _pivotAngleX + 90.0F;
            if (angleX > 360.0F)
            {
                angleX -= 360.0F;
            }
            const float theta = DegreesToRadians(angleY);
            const float phi = DegreesToRadians(angleX);
            const float x = RoundToEven(_pivotDistance * std::cos(theta) * 10000.0F) / 10000.0F;
            const float y = -RoundToEven(_pivotDistance * std::sin(theta) * std::cos(phi) * 10000.0F) / 10000.0F;
            const float z = RoundToEven(_pivotDistance * std::sin(theta) * std::sin(phi) * 10000.0F) / 10000.0F;
            _cameraPosition = Vector3(x, y, z);
        }
        else if (_cameraMode == MphRead::CameraMode::Player)
        {
            _cameraPosition = RequireReference(Entities::PlayerEntity::Main()->CameraInfo()).Position;
        }
    }

    void Scene::ResetCamera()
    {
        if (_cameraMode == MphRead::CameraMode::Roam)
        {
            _cameraPosition = Vector3{};
            _cameraFacing = Vector3(0.0F, 0.0F, -1.0F);
            _cameraUp = Vector3(0.0F, 1.0F, 0.0F);
            _cameraRight = Vector3(1.0F, 0.0F, 0.0F);
        }
        else if (_cameraMode == MphRead::CameraMode::Pivot)
        {
            _pivotAngleX = 0.0F;
            _pivotAngleY = 0.0F;
            _pivotDistance = 5.0F;
        }
    }

    void Scene::UpdateCameraRotation(float stepH, float stepV)
    {
        const float angleH = std::atan2(_cameraFacing.X, -_cameraFacing.Z) + stepH;
        float angleV = std::asin(_cameraFacing.Y) + stepV;
        angleV = std::clamp(angleV, -_almostHalfPi, _almostHalfPi);
        _cameraFacing = Vector3(
            std::cos(angleV) * std::sin(angleH),
            std::sin(angleV),
            -(std::cos(angleV) * std::cos(angleH))).Normalized();
        _cameraRight = Vector3::Cross(_cameraFacing, Vector3(0.0F, 1.0F, 0.0F));
        _cameraUp = Vector3::Cross(_cameraRight, _cameraFacing);
    }

    void Scene::StartCutscene(std::int32_t id)
    {
        if (_activeCutscene == -1)
        {
            _activeCutscene = id;
            _priorCameraPos = _cameraPosition;
            _priorCameraFacing = _cameraFacing;
            _priorCameraFov = _cameraFov;
        }
    }

    void Scene::EndCutscene(bool resetFade)
    {
        if (_activeCutscene != -1)
        {
            _activeCutscene = -1;
            _cameraPosition = _priorCameraPos;
            _cameraFacing = _priorCameraFacing;
            _cameraRight = Vector3::Cross(_cameraFacing, Vector3(0.0F, 1.0F, 0.0F));
            _cameraUp = Vector3::Cross(_cameraRight, _cameraFacing);
            _cameraFov = _priorCameraFov;
        }
        if (resetFade)
        {
            SetFade(MphRead::FadeType::None, 0.0F, true);
        }
    }

    void Scene::ResetFrameCount()
    {
        _frameCount = 0;
    }

    void Scene::AllocateEffects()
    {
        for (std::int32_t i = 0; i < _effectEntryMax; ++i)
        {
            _inactiveEffects.push(std::make_shared<EffectEntry>());
        }
        for (std::int32_t i = 0; i < _effectElementMax; ++i)
        {
            _inactiveElements.push(std::make_shared<EffectElementEntry>());
        }
        for (std::int32_t i = 0; i < _effectParticleMax; ++i)
        {
            _inactiveParticles.push(std::make_shared<EffectParticle>());
        }
        _singleParticles.reserve(_singleParticleMax);
        for (std::int32_t i = 0; i < _singleParticleMax; ++i)
        {
            _singleParticles.push_back(std::make_shared<SingleParticle>());
        }
        for (std::int32_t i = 0; i < _beamEffectMax; ++i)
        {
            _inactiveBeamEffects.push(std::make_shared<Entities::BeamEffectEntity>(this));
        }
        for (std::int32_t i = 0; i < _bombMax; ++i)
        {
            _inactiveBombs.push(std::make_shared<Entities::BombEntity>(this));
        }
    }

    std::shared_ptr<Entities::BeamEffectEntity> Scene::InitBeamEffect(
        const Entities::BeamEffectEntityData& data)
    {
        if (_inactiveBeamEffects.empty())
        {
            return nullptr;
        }
        auto entry = _inactiveBeamEffects.front();
        _inactiveBeamEffects.pop();
        entry->Spawn(data);
        return entry;
    }

    void Scene::UnlinkBeamEffect(std::shared_ptr<Entities::BeamEffectEntity> entry)
    {
        // Taken by value: a caller may pass a slot of _activeBeamEffects itself,
        // which RemoveFirst shifts. C# passes the object, not the slot.
        RemoveFirst(_activeBeamEffects, entry);
        _inactiveBeamEffects.push(entry);
    }

    void Scene::UnlinkBeamEffect(Entities::BeamEffectEntity* entry)
    {
        // Not looked up in the scene: RoomEntity::StartTransition removes every
        // entity before destroying it, so an effect still alive when the room
        // changes is no longer there -- and the lookup threw on every map
        // rotation that followed a shot.
        if (entry == nullptr)
        {
            throw System::NullReferenceException();
        }
        UnlinkBeamEffect(Entities::SharedFrom(entry));
    }

    std::shared_ptr<Entities::BombEntity> Scene::InitBomb()
    {
        if (_inactiveBombs.empty())
        {
            return nullptr;
        }
        auto entry = _inactiveBombs.front();
        _inactiveBombs.pop();
        return entry;
    }

    void Scene::UnlinkBomb(std::shared_ptr<Entities::BombEntity> entry)
    {
        // Taken by value, as UnlinkBeamEffect is.
        RemoveFirst(_activeBombs, entry);
        _inactiveBombs.push(entry);
    }

    void Scene::UnlinkBomb(Entities::BombEntity* entry)
    {
        // As UnlinkBeamEffect: a bomb destroyed by a room change has already
        // been removed from the scene.
        if (entry == nullptr)
        {
            throw System::NullReferenceException();
        }
        UnlinkBomb(Entities::SharedFrom(entry));
    }

    void Scene::AddSingleParticle(SingleType type, Vector3 position, Vector3 color, float alpha, float scale)
    {
        if (_singleParticleCount < _singleParticleMax)
        {
            auto entry = _singleParticles[static_cast<std::size_t>(_singleParticleCount++)];
            entry->ParticleDefinition = Read::GetSingleParticle(type);
            entry->Position = position;
            entry->Color = color;
            entry->Alpha = alpha;
            entry->Scale = scale;
            if (!_texPalMap.contains(entry->ParticleDefinition->Model->Id))
            {
                InitTextures(entry->ParticleDefinition->Model);
            }
        }
    }

    std::shared_ptr<EffectEntry> Scene::InitEffectEntry()
    {
        if (_inactiveEffects.empty())
        {
            return nullptr;
        }
        auto entry = _inactiveEffects.front();
        _inactiveEffects.pop();
        if (Mods::DebugLog::Active())
        {
            PoolTrace().FreeEntries.erase(entry.get());
        }
        entry->EffectId = 0;
        MPHREAD_DEBUG_ASSERT(entry->Elements->empty());
        return entry;
    }

    void Scene::UnlinkEffectEntry(const std::shared_ptr<EffectEntry>& entry)
    {
        if (Mods::DebugLog::Active())
        {
            // Diagnostic only: an entry released while already free is then
            // lent to two owners at once, and either one's release takes the
            // other's elements with it.
            if (!PoolTrace().FreeEntries.insert(entry.get()).second)
            {
                Mods::DebugLog::Stack("effects", "effect entry released while already free: effect "
                    + std::to_string(entry->EffectId));
                const auto previous = PoolTrace().LastRelease.find(entry.get());
                if (previous != PoolTrace().LastRelease.end())
                {
                    Mods::DebugLog::StackFrom("effects", "   ... it had already been released here:", previous->second);
                }
            }
            PoolTrace().LastRelease[entry.get()] = Mods::DebugLog::CaptureStack();
        }
        for (std::size_t i = 0; i < entry->Elements->size(); ++i)
        {
            UnlinkEffectElement(entry->Elements->at(i));
        }
        entry->Elements->clear();
        _inactiveEffects.push(entry);
    }

    void Scene::DetachEffectEntry(const std::shared_ptr<EffectEntry>& entry, bool setExpired)
    {
        for (std::size_t i = 0; i < entry->Elements->size(); ++i)
        {
            auto element = entry->Elements->at(i);
            if (TypeExtensions::TestFlag(element->Flags, EffElemFlags::DestroyOnDetach))
            {
                UnlinkEffectElement(element);
            }
            else
            {
                element->Flags &= ~EffElemFlags::ElementExtension;
                element->Flags |= EffElemFlags::KeepAlive;
                element->EffectEntry.reset();
                if (setExpired)
                {
                    element->Expired = true;
                }
            }
        }
        entry->Elements->clear();
        UnlinkEffectEntry(entry);
    }

    std::shared_ptr<EffectElementEntry> Scene::InitEffectElement(const std::shared_ptr<Effect>& effect,
        const std::shared_ptr<EffectElement>& element, std::shared_ptr<Formats::Collision::EntityCollision> entCol,
        bool child)
    {
        if (_inactiveElements.empty())
        {
            return nullptr;
        }
        auto entry = _inactiveElements.front();
        _inactiveElements.pop();
        if (Mods::DebugLog::Active()
            && std::find(_activeElements.begin(), _activeElements.end(), entry) != _activeElements.end())
        {
            // Diagnostic only: the free pool handed out an element in use.
            Mods::DebugLog::Stack("effects", "effect element handed out while still active: was effect "
                + std::to_string(entry->EffectId) + " \"" + entry->EffectName + "/" + entry->ElementName
                + "\", now effect " + std::to_string(effect->Id) + " \"" + effect->Name + "/" + element->Name + "\"");
        }
        entry->EffectId = effect->Id;
        entry->EffectName = effect->Name;
        entry->ElementName = element->Name;
        entry->BufferTime = element->BufferTime;
        entry->CreationTime = _elapsedTime + (child ? (1.0F / 60.0F) : 0.0F);
        entry->DrainTime = element->DrainTime;
        entry->DrawType = element->DrawType;
        entry->Lifespan = element->Lifespan;
        entry->ExpirationTime = entry->CreationTime + entry->Lifespan;
        entry->Flags = element->Flags | EffElemFlags::DrawEnabled;
        entry->Func39Called = false;
        entry->SetFuncs(element->Funcs);
        entry->SetActions(element->Actions);
        entry->OwnTransform = RendererDetail::IdentityMatrix();
        entry->Transform = RendererDetail::IdentityMatrix();
        entry->ParticleAmount = 0.0F;
        entry->Expired = false;
        entry->ChildEffectId = static_cast<std::int32_t>(element->ChildEffectId);
        entry->Acceleration = element->Acceleration;
        entry->ParticleDefinitions->insert(entry->ParticleDefinitions->end(), element->Particles->begin(), element->Particles->end());
        entry->Parity = static_cast<std::int32_t>(_effectFrame % 2U);
        entry->EffectEntry.reset();
        entry->EntityCollision = std::move(entCol);
        entry->Definition = element;
        entry->RoField1 = entry->RoField2 = entry->RoField3 = entry->RoField4 = 0.0F;
        _activeElements.push_back(entry);
        return entry;
    }

    void Scene::UnlinkEffectElement(std::shared_ptr<EffectElementEntry> element)
    {
        // Taken by value. ClearEffects passes _activeElements[i] itself, and
        // RemoveFirst below shifts the vector: a reference would then name the
        // next, still active element, and it is that one this function would
        // clear and hand back to the pool -- the pool then held one element
        // twice, and the second user of it found ParticleDefinitions empty.
        // C# passes the object, never the slot.
        if (Mods::DebugLog::Active()
            && std::find(_activeElements.begin(), _activeElements.end(), element) == _activeElements.end())
        {
            // Diagnostic only: a released element must be an active one, or the
            // free pool ends up holding it twice.
            Mods::DebugLog::Stack("effects", "effect element released while not active: effect "
                + std::to_string(element->EffectId) + " \"" + element->EffectName + "/"
                + element->ElementName + "\"");
            const auto previous = PoolTrace().LastRelease.find(element.get());
            if (previous != PoolTrace().LastRelease.end())
            {
                Mods::DebugLog::StackFrom("effects", "   ... it had already been released here:", previous->second);
            }
        }
        if (Mods::DebugLog::Active())
        {
            PoolTrace().LastRelease[element.get()] = Mods::DebugLog::CaptureStack();
        }
        while (!element->Particles->empty())
        {
            auto particle = element->Particles->front();
            element->Particles->erase(element->Particles->begin());
            UnlinkEffectParticle(particle);
        }
        RemoveFirst(_activeElements, element);
        element->EntityCollision.reset();
        element->Definition.reset();
        element->Model.reset();
        element->Nodes->clear();
        element->EffectName.clear();
        element->ElementName.clear();
        element->ParticleDefinitions->clear();
        element->TextureBindingIds->clear();
        MPHREAD_DEBUG_ASSERT(element->Particles->empty());
        _inactiveElements.push(element);
    }

    std::shared_ptr<EffectParticle> Scene::InitEffectParticle()
    {
        if (_inactiveParticles.empty())
        {
            return nullptr;
        }
        auto particle = _inactiveParticles.front();
        _inactiveParticles.pop();
        particle->Position = Vector3{};
        particle->Speed = Vector3{};
        particle->ParticleId = 0;
        particle->RoField1 = particle->RoField2 = particle->RoField3 = particle->RoField4 = 0.0F;
        particle->RwField1 = particle->RwField2 = particle->RwField3 = particle->RwField4 = 0.0F;
        particle->CreationTime = _elapsedTime;
        return particle;
    }

    void Scene::UnlinkEffectParticle(const std::shared_ptr<EffectParticle>& particle)
    {
        _inactiveParticles.push(particle);
    }

    void Scene::LoadEffect(std::int32_t effectId, bool persistent)
    {
        const auto effect = Read::LoadEffect(effectId, persistent);
        for (const auto& element : *effect->Elements)
        {
            const auto model = Read::GetModelInstance(element->ModelName)->Model();
            InitTextures(model);
            GenerateGpuMeshes(model, false);
        }
    }

    std::shared_ptr<EffectEntry> Scene::SpawnEffectGetEntry(std::int32_t effectId,
        Vector3 facing, Vector3 up, Vector3 position, std::shared_ptr<Formats::Collision::EntityCollision> entCol)
    {
        const Matrix4 transform = Entities::EntityBase::GetTransformMatrix(facing, up, position);
        return SpawnEffectGetEntry(effectId, transform, std::move(entCol));
    }

    std::shared_ptr<EffectEntry> Scene::SpawnEffectGetEntry(std::int32_t effectId,
        Matrix4 transform, std::shared_ptr<Formats::Collision::EntityCollision> entCol)
    {
        auto entry = InitEffectEntry();
        if (!entry)
        {
            return nullptr;
        }
        entry->EffectId = effectId;
        SpawnEffect(effectId, transform, false, entry, std::move(entCol));
        return entry;
    }

    void Scene::SpawnEffect(std::int32_t effectId, Vector3 facing, Vector3 up, Vector3 position,
        bool child, std::shared_ptr<Formats::Collision::EntityCollision> entCol)
    {
        SpawnEffect(effectId, Entities::EntityBase::GetTransformMatrix(facing, up, position), child, nullptr,
            std::move(entCol));
    }

    void Scene::SpawnEffect(std::int32_t effectId, Matrix4 transform, bool child,
        std::shared_ptr<Formats::Collision::EntityCollision> entCol)
    {
        SpawnEffect(effectId, transform, child, nullptr, std::move(entCol));
    }

    void Scene::SpawnEffect(std::int32_t effectId, Matrix4 transform, bool child,
        const std::shared_ptr<EffectEntry>& entry, std::shared_ptr<Formats::Collision::EntityCollision> entCol)
    {
        const auto effect = Read::GetEffect(effectId);
        if (!effect)
        {
            MPHREAD_DEBUG_ASSERT(effectId == 162);
            return;
        }
        for (const auto& elementDef : *effect->Elements)
        {
            auto element = InitEffectElement(effect, elementDef, entCol, child);
            if (!element)
            {
                return;
            }
            if (entry)
            {
                element->EffectEntry = entry;
                entry->Elements->push_back(element);
            }
            if (TypeExtensions::TestFlag(element->Flags, EffElemFlags::SpawnUnitVecs))
            {
                const Vector3 vec1(0.0F, 1.0F, 0.0F);
                const Vector3 vec2(1.0F, 0.0F, 0.0F);
                transform = Matrix::GetTransform4(vec2, vec1, Vector3(transform.M41, transform.M42, transform.M43));
            }
            element->Transform = transform;
            element->OwnTransform = transform;
            for (std::size_t j = 0; j < elementDef->Particles->size(); ++j)
            {
                const auto particleDef = elementDef->Particles->at(j);
                if (j == 0)
                {
                    if (!_texPalMap.contains(particleDef->Model->Id))
                    {
                        InitTextures(particleDef->Model);
                    }
                    element->Model = particleDef->Model;
                }
                element->Nodes->push_back(particleDef->Node);
                auto& material = *particleDef->Model->Materials->at(static_cast<std::size_t>(particleDef->MaterialId));
                material.TextureBindingId = Mods::Headless::Active() ? 0
                    : _texPalMap.at(particleDef->Model->Id)->Get(material.TextureId, material.PaletteId, 0).BindingId;
                element->TextureBindingIds->push_back(material.TextureBindingId);
            }
        }
    }

    std::int32_t Scene::CountElements(std::int32_t effectId)
    {
        const auto effect = Read::GetEffect(effectId);
        if (!effect)
        {
            return 0;
        }
        std::int32_t count = 0;
        for (const auto& element : *effect->Elements)
        {
            for (const auto& active : _activeElements)
            {
                if (active->Definition == element)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    void Scene::ClearEffects()
    {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_activeElements.size()); ++i)
        {
            const auto element = _activeElements[static_cast<std::size_t>(i)];
            ReleaseFromOwner(element);
            UnlinkEffectElement(element);
            --i;
        }
    }

    void Scene::BreakEffectCycles()
    {
        // An element owns its particles and each particle points back at its
        // element; an element and its entry point at each other. The pools
        // outlive nothing but this scene, so a discarded scene has to cut
        // them or every element it ever made stays alive with its particle
        // definitions and their models.
        ClearEffects();
        const auto cut = [](const std::shared_ptr<EffectElementEntry>& element)
        {
            if (!element) return;
            for (const auto& particle : *element->Particles)
            {
                if (particle) particle->Owner.reset();
            }
            element->Particles->clear();
            element->EffectEntry.reset();
        };
        for (const auto& element : _activeElements) cut(element);
        _activeElements.clear();
        for (; !_inactiveElements.empty(); _inactiveElements.pop()) cut(_inactiveElements.front());
        for (; !_inactiveParticles.empty(); _inactiveParticles.pop())
        {
            if (_inactiveParticles.front()) _inactiveParticles.front()->Owner.reset();
        }
        for (; !_inactiveEffects.empty(); _inactiveEffects.pop())
        {
            if (_inactiveEffects.front()) _inactiveEffects.front()->Elements->clear();
        }
    }

    void Scene::ReleaseFromOwner(const std::shared_ptr<EffectElementEntry>& element)
    {
        // C# Scene.ReleaseFromOwner: a bulk release takes the element out of
        // the entry that still holds it, so the owner's own release later does
        // not release it a second time.
        if (element->EffectEntry)
        {
            RemoveFirst(*element->EffectEntry->Elements, element);
            element->EffectEntry.reset();
        }
    }

    void Scene::ClearNonPersistentEffects()
    {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_activeElements.size()); ++i)
        {
            const auto element = _activeElements[static_cast<std::size_t>(i)];
            const auto effect = Read::GetEffect(element->EffectId);
            if (!effect || !effect->Persistent)
            {
                ReleaseFromOwner(element);
                UnlinkEffectElement(element);
                --i;
            }
        }
    }

    std::int64_t Scene::ModEffectParticles() const noexcept
    {
        return _modEffectParticles;
    }

    void Scene::ProcessEffects(std::uint64_t effectFrame)
    {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_activeElements.size()); ++i)
        {
            auto element = _activeElements[static_cast<std::size_t>(i)];
            if (!element->Expired && _elapsedTime > element->ExpirationTime)
            {
                if (!element->EffectEntry && !TypeExtensions::TestFlag(element->Flags, EffElemFlags::KeepAlive))
                {
                    UnlinkEffectElement(element);
                    --i;
                    continue;
                }
                element->Expired = true;
            }
            if (element->Expired)
            {
                if (!element->EffectEntry && element->Particles->empty())
                {
                    UnlinkEffectElement(element);
                    --i;
                    continue;
                }
                element->Transform = element->OwnTransform;
            }
            else
            {
                if (TypeExtensions::TestFlag(element->Flags, EffElemFlags::ElementExtension)
                    && _elapsedTime - element->CreationTime > element->BufferTime)
                {
                    element->CreationTime += element->BufferTime - element->DrainTime;
                    element->ExpirationTime += element->BufferTime - element->DrainTime;
                }
                element->Transform = element->EntityCollision
                    ? Multiply(element->OwnTransform, element->EntityCollision->Transform)
                    : element->OwnTransform;
                TimeValues times(_elapsedTime, _elapsedTime - element->CreationTime, element->Lifespan);
                auto action = element->Actions()->find(FuncAction::IncreaseParticleAmount);
                if (effectFrame % 2U == static_cast<std::uint64_t>(element->Parity)
                    && action != element->Actions()->end())
                {
                    element->ParticleAmount += element->InvokeFloatFunc(action->second, times);
                }
                const std::int32_t spawnCount = ::MphRead::NativeRuntime::ConvertToInt32Net9(
                    std::floor(element->ParticleAmount));
                element->ParticleAmount -= static_cast<float>(spawnCount);
                float portionTotal = 0.0F;
                for (std::int32_t j = 0; j < spawnCount; ++j)
                {
                    Vector3 temp{};
                    auto particle = InitEffectParticle();
                    if (!particle)
                    {
                        break;
                    }
                    element->Particles->push_back(particle);
                    ++_modEffectParticles;
                    particle->Owner = element;
                    particle->SetFuncIds();
                    particle->PortionTotal = portionTotal;
                    if (element->ParticleDefinitions->empty() && Mods::DebugLog::Active())
                    {
                        // Diagnostic only; the at(0) below still throws as C#'s [0] would.
                        const auto copies = std::count(_activeElements.begin(), _activeElements.end(), element);
                        Mods::DebugLog::Line("effects", "effect element with no particle definitions spawns: effect "
                            + std::to_string(element->EffectId) + " \"" + element->EffectName + "/" + element->ElementName
                            + "\", definition particles "
                            + (element->Definition ? std::to_string(element->Definition->Particles->size()) : std::string("none"))
                            + ", owned by an entry " + (element->EffectEntry ? "yes" : "no")
                            + ", in the active list " + std::to_string(copies) + " time(s)");
                    }
                    particle->MaterialId = element->ParticleDefinitions->at(0)->MaterialId;
                    auto info = element->Actions()->find(FuncAction::SetNewParticlePosition);
                    if (info != element->Actions()->end())
                    {
                        particle->InvokeVecFunc(info->second, times, temp);
                        particle->Position = temp;
                    }
                    info = element->Actions()->find(FuncAction::SetNewParticleSpeed);
                    if (info != element->Actions()->end())
                    {
                        particle->InvokeVecFunc(info->second, times, temp);
                        particle->Speed = temp;
                    }
                    if (!TypeExtensions::TestFlag(element->Flags, EffElemFlags::UseTransform))
                    {
                        particle->Position = Matrix::Vec3MultMtx4(particle->Position, element->Transform);
                        particle->Speed = Matrix::Vec3MultMtx3(particle->Speed, element->Transform);
                    }
                    const auto setRo = [&](FuncAction func, float fallback, float& target)
                    {
                        auto found = element->Actions()->find(func);
                        target = found != element->Actions()->end()
                            ? particle->InvokeFloatFunc(found->second, times)
                            : fallback;
                    };
                    setRo(FuncAction::SetParticleRoField1, element->RoField1, particle->RoField1);
                    setRo(FuncAction::SetParticleRoField2, element->RoField2, particle->RoField2);
                    setRo(FuncAction::SetParticleRoField3, element->RoField3, particle->RoField3);
                    setRo(FuncAction::SetParticleRoField4, element->RoField4, particle->RoField4);
                    info = element->Actions()->find(FuncAction::SetNewParticleLifespan);
                    if (info != element->Actions()->end())
                    {
                        TimeValues tempTimes(_elapsedTime, 1.0F, element->Lifespan);
                        particle->Lifespan = particle->InvokeFloatFunc(info->second, tempTimes);
                        particle->ExpirationTime = particle->CreationTime + particle->Lifespan;
                    }
                    else
                    {
                        particle->Lifespan = element->Lifespan;
                        particle->ExpirationTime = element->ExpirationTime;
                    }
                    info = element->Actions()->find(FuncAction::UpdateParticleSpeed);
                    if (info != element->Actions()->end() && info->second->FuncId == 4)
                    {
                        Vector3 speed = particle->Speed;
                        particle->InvokeVecFunc(info->second, times, speed);
                        particle->Speed = speed;
                    }
                    const auto setOneTime = [&](FuncAction func, float defaultValue, float& target, bool clampAlpha = false)
                    {
                        auto found = element->Actions()->find(func);
                        if (found != element->Actions()->end() && found->second->FuncId == 42)
                        {
                            target = particle->InvokeFloatFunc(found->second, times);
                            if (clampAlpha && target < 0.0F)
                            {
                                target = 0.0F;
                            }
                        }
                        else
                        {
                            target = defaultValue;
                        }
                    };
                    setOneTime(FuncAction::SetParticleRed, 1.0F, particle->Red);
                    setOneTime(FuncAction::SetParticleGreen, 1.0F, particle->Green);
                    setOneTime(FuncAction::SetParticleBlue, 1.0F, particle->Blue);
                    setOneTime(FuncAction::SetParticleAlpha, 1.0F, particle->Alpha, true);
                    setOneTime(FuncAction::SetParticleScale, 0.0F, particle->Scale);
                    info = element->Actions()->find(FuncAction::SetParticleRotation);
                    if (info != element->Actions()->end() && info->second->FuncId == 42)
                    {
                        particle->Rotation = particle->InvokeFloatFunc(info->second, times);
                    }
                    const auto setRw = [&](FuncAction func, float& target)
                    {
                        auto found = element->Actions()->find(func);
                        if (found != element->Actions()->end())
                        {
                            target = particle->InvokeFloatFunc(found->second, times);
                        }
                    };
                    setRw(FuncAction::SetParticleRwField1, particle->RwField1);
                    setRw(FuncAction::SetParticleRwField2, particle->RwField2);
                    setRw(FuncAction::SetParticleRwField3, particle->RwField3);
                    setRw(FuncAction::SetParticleRwField4, particle->RwField4);
                    portionTotal += 1.0F / static_cast<float>(spawnCount);
                }
            }

            for (std::int32_t j = 0; j < static_cast<std::int32_t>(element->Particles->size()); ++j)
            {
                auto particle = element->Particles->at(static_cast<std::size_t>(j));
                if (TypeExtensions::TestFlag(element->Flags, EffElemFlags::ElementExtension)
                    && TypeExtensions::TestFlag(element->Flags, EffElemFlags::ParticleExtension)
                    && _elapsedTime - particle->CreationTime > element->BufferTime)
                {
                    particle->CreationTime += element->BufferTime - element->DrainTime;
                    particle->ExpirationTime += element->BufferTime - element->DrainTime;
                }
                if (_elapsedTime < particle->ExpirationTime)
                {
                    TimeValues times(_elapsedTime, _elapsedTime - particle->CreationTime, particle->Lifespan);
                    const auto updateFloat = [&](FuncAction func, float& target)
                    {
                        auto found = element->Actions()->find(func);
                        if (found != element->Actions()->end())
                        {
                            target = particle->InvokeFloatFunc(found->second, times);
                        }
                    };
                    updateFloat(FuncAction::SetParticleRwField1, particle->RwField1);
                    updateFloat(FuncAction::SetParticleRwField2, particle->RwField2);
                    updateFloat(FuncAction::SetParticleRwField3, particle->RwField3);
                    updateFloat(FuncAction::SetParticleRwField4, particle->RwField4);
                    auto info = element->Actions()->find(FuncAction::SetParticleId);
                    if (info != element->Actions()->end())
                    {
                        particle->ParticleId = ::MphRead::NativeRuntime::ConvertToInt32Net9(
                            particle->InvokeFloatFunc(info->second, times));
                        if (particle->ParticleId >= static_cast<std::int32_t>(element->ParticleDefinitions->size()))
                        {
                            particle->ParticleId = static_cast<std::int32_t>(element->ParticleDefinitions->size()) - 1;
                        }
                        particle->MaterialId = element->ParticleDefinitions->at(static_cast<std::size_t>(particle->ParticleId))->MaterialId;
                    }
                    info = element->Actions()->find(FuncAction::UpdateParticleSpeed);
                    if (info != element->Actions()->end())
                    {
                        Vector3 speed = particle->Speed;
                        particle->InvokeVecFunc(info->second, times, speed);
                        particle->Speed = speed;
                    }
                    updateFloat(FuncAction::SetParticleRed, particle->Red);
                    updateFloat(FuncAction::SetParticleGreen, particle->Green);
                    updateFloat(FuncAction::SetParticleBlue, particle->Blue);
                    updateFloat(FuncAction::SetParticleAlpha, particle->Alpha);
                    if (particle->Alpha < 0.0F)
                    {
                        particle->Alpha = 0.0F;
                    }
                    updateFloat(FuncAction::SetParticleScale, particle->Scale);
                    updateFloat(FuncAction::SetParticleRotation, particle->Rotation);
                    if (TypeExtensions::TestFlag(element->Flags, EffElemFlags::UseAcceleration))
                    {
                        particle->Speed = Vector3(
                            particle->Speed.X + element->Acceleration.X * (1.0F / 60.0F),
                            particle->Speed.Y + element->Acceleration.Y * (1.0F / 60.0F),
                            particle->Speed.Z + element->Acceleration.Z * (1.0F / 60.0F));
                    }
                    const Vector3 prevPos = particle->Position;
                    particle->Position = Vector3(
                        particle->Position.X + particle->Speed.X * (1.0F / 60.0F),
                        particle->Position.Y + particle->Speed.Y * (1.0F / 60.0F),
                        particle->Position.Z + particle->Speed.Z * (1.0F / 60.0F));
                    if (TypeExtensions::TestFlag(element->Flags, EffElemFlags::CheckCollision))
                    {
                        Formats::CollisionResult result{};
                        if (CollisionDetection::CheckBetweenPoints(prevPos, particle->Position,
                            TestFlags::None, this, result))
                        {
                            particle->Position = result.Position;
                            particle->ExpirationTime = _elapsedTime;
                        }
                    }
                }
                else
                {
                    if (TypeExtensions::TestFlag(element->Flags, EffElemFlags::SpawnChildEffect) && element->ChildEffectId != 0)
                    {
                        Vector3 vec1 = Negate(particle->Speed).Normalized();
                        Vector3 vec2 = (vec1.Z <= Fixed::ToFloat(-3686) || vec1.Z >= Fixed::ToFloat(3686))
                            ? Vector3(1.0F, 0.0F, 0.0F)
                            : Vector3(0.0F, 0.0F, 1.0F);
                        vec2 = Vector3::Cross(vec1, vec2).Normalized();
                        SpawnEffect(element->ChildEffectId,
                            Matrix::GetTransform4(vec2, vec1, particle->Position));
                    }
                    element->Particles->erase(element->Particles->begin() + j);
                    UnlinkEffectParticle(particle);
                    --j;
                }
            }
        }
    }

    std::shared_ptr<RenderItem> Scene::GetRenderItem()
    {
        if (!_freeRenderItems.empty())
        {
            auto item = _freeRenderItems.front();
            _freeRenderItems.pop();
            return item;
        }
        return std::make_shared<MphRead::RenderItem>();
    }

    void Scene::AddRenderItem(const Material& material, std::int32_t polygonId, float alphaScale,
        Vector3 emission, const LightInfo& lightInfo, Matrix4 texcoordMatrix, Matrix4 transform,
        const std::shared_ptr<Model>& model, const std::shared_ptr<Mesh>& mesh,
        std::int32_t matrixStackCount, const std::vector<float>& matrixStack,
        std::optional<Vector4> overrideColor, std::optional<Vector4> paletteOverride,
        SelectionType selectionType, BillboardMode billboardMode, float scaleFactor,
        std::optional<std::int32_t> bindingOverride)
    {
        transform.M11 *= scaleFactor; transform.M12 *= scaleFactor; transform.M13 *= scaleFactor;
        transform.M21 *= scaleFactor; transform.M22 *= scaleFactor; transform.M23 *= scaleFactor;
        transform.M31 *= scaleFactor; transform.M32 *= scaleFactor; transform.M33 *= scaleFactor;
        _scaleFactors = {scaleFactor, scaleFactor, scaleFactor, 1.0F,
            scaleFactor, scaleFactor, scaleFactor, 1.0F,
            scaleFactor, scaleFactor, scaleFactor, 1.0F,
            1.0F, 1.0F, 1.0F, 1.0F};
        auto item = GetRenderItem();
        item->Type = RenderItemType::Mesh;
        item->PolygonId = polygonId;
        item->Alpha = material.CurrentAlpha * alphaScale;
        item->PolygonMode = material.PolygonMode;
        item->RenderMode = material.RenderMode;
        item->CullingMode = material.Culling;
        item->BillboardMode = billboardMode;
        item->Wireframe = material.Wireframe != 0;
        item->Lighting = material.Lighting != 0;
        item->NoLines = false;
        item->Diffuse = material.CurrentDiffuse;
        item->Ambient = material.CurrentAmbient;
        item->Specular = material.CurrentSpecular;
        item->Emission = emission;
        item->LightInfo = lightInfo;
        if (bindingOverride.has_value())
        {
            item->TexgenMode = TexgenMode::Normal;
            item->XRepeat = RepeatMode::Mirror;
            item->YRepeat = RepeatMode::Mirror;
            item->HasTexture = true;
            item->TextureBindingId = *bindingOverride;
        }
        else
        {
            item->TexgenMode = material.TexgenMode;
            item->XRepeat = material.XRepeat;
            item->YRepeat = material.YRepeat;
            item->HasTexture = material.TextureId != -1;
            item->TextureBindingId = material.TextureBindingId;
        }
        item->TexcoordMatrix = texcoordMatrix;
        item->Transform = transform;
        item->MeshModel = model;
        item->MeshObject = mesh;
        MPHREAD_DEBUG_ASSERT(matrixStack.size() == static_cast<std::size_t>(16 * matrixStackCount));
        item->MatrixStackCount = matrixStackCount;
        for (std::size_t i = 0; i < matrixStack.size(); ++i)
        {
            (*item->MatrixStack)[i] = matrixStack[i] * _scaleFactors[i - (i / 16U) * 16U];
        }
        item->OverrideColor = overrideColor;
        item->PaletteOverride = paletteOverride;
        item->Points = ManagedArray<Vector3>::Empty();
        item->ScaleS = 1.0F;
        item->ScaleT = 1.0F;
        if (selectionType != SelectionType::None)
        {
            overrideColor = Selection::GetSelectionColor(selectionType);
            if (overrideColor.has_value())
            {
                item->OverrideColor = overrideColor;
                item->PaletteOverride.reset();
            }
        }
        AddRenderItem(item);
    }

    void Scene::AddRenderItem(CullingMode cullingMode, std::int32_t polygonId, Vector4 overrideColor,
        RenderItemType type, std::shared_ptr<ManagedArray<Vector3>> vertices,
        std::int32_t vertexCount, bool noLines)
    {
        auto item = GetRenderItem();
        item->Type = type;
        item->PolygonId = polygonId;
        item->Alpha = 1.0F;
        item->PolygonMode = PolygonMode::Modulate;
        item->RenderMode = RenderMode::Translucent;
        item->CullingMode = cullingMode;
        item->BillboardMode = BillboardMode::None;
        item->Wireframe = false;
        item->Lighting = false;
        item->NoLines = noLines;
        item->Diffuse = Vector3{};
        item->Ambient = Vector3{};
        item->Specular = Vector3{};
        item->Emission = Vector3{};
        item->LightInfo = LightInfo::Zero;
        item->TexgenMode = TexgenMode::None;
        item->XRepeat = RepeatMode::Clamp;
        item->YRepeat = RepeatMode::Clamp;
        item->HasTexture = false;
        item->TextureBindingId = 0;
        item->TexcoordMatrix = RendererDetail::IdentityMatrix();
        item->Transform = RendererDetail::IdentityMatrix();
        item->MeshModel.reset();
        item->MeshObject.reset();
        item->MatrixStackCount = 0;
        item->OverrideColor = overrideColor;
        item->PaletteOverride.reset();
        item->Points = std::move(vertices);
        item->ScaleS = 1.0F;
        item->ScaleT = 1.0F;
        MPHREAD_DEBUG_ASSERT(type != RenderItemType::Ngon || vertexCount >= 3);
        item->ItemCount = vertexCount;
        AddRenderItem(item);
    }

    void Scene::AddRenderItem(RenderItemType type, float alpha, std::int32_t polygonId, Vector3 color,
        RepeatMode xRepeat, RepeatMode yRepeat, float scaleS, float scaleT, Matrix4 transform,
        std::shared_ptr<ManagedArray<Vector3>> uvsAndVerts, std::int32_t bindingId,
        BillboardMode billboardMode, std::int32_t trailCount)
    {
        auto item = GetRenderItem();
        item->Type = type;
        item->PolygonId = polygonId;
        item->Alpha = alpha;
        item->PolygonMode = PolygonMode::Modulate;
        item->RenderMode = RenderMode::Translucent;
        item->CullingMode = CullingMode::Neither;
        item->BillboardMode = billboardMode;
        item->Wireframe = false;
        item->Lighting = false;
        item->NoLines = false;
        item->Diffuse = color;
        item->Ambient = Vector3{};
        item->Specular = Vector3{};
        item->Emission = Vector3{};
        item->LightInfo = LightInfo::Zero;
        item->TexgenMode = TexgenMode::None;
        item->XRepeat = xRepeat;
        item->YRepeat = yRepeat;
        item->HasTexture = true;
        item->TextureBindingId = bindingId;
        item->TexcoordMatrix = RendererDetail::IdentityMatrix();
        item->Transform = transform;
        item->MeshModel.reset();
        item->MeshObject.reset();
        item->MatrixStackCount = 0;
        item->OverrideColor.reset();
        item->PaletteOverride.reset();
        item->Points = std::move(uvsAndVerts);
        item->ScaleS = scaleS;
        item->ScaleT = scaleT;
        item->ItemCount = trailCount;
        AddRenderItem(item);
    }

    void Scene::AddRenderItem(RenderItemType type, std::int32_t polygonId, Vector3 color,
        RepeatMode xRepeat, RepeatMode yRepeat, float scaleS, float scaleT,
        std::int32_t matrixStackCount, const std::vector<float>& matrixStack,
        std::shared_ptr<ManagedArray<Vector3>> uvsAndVerts, std::int32_t segmentCount,
        std::int32_t bindingId)
    {
        auto item = GetRenderItem();
        item->Type = type;
        item->PolygonId = polygonId;
        item->Alpha = 1.0F;
        item->PolygonMode = PolygonMode::Modulate;
        item->RenderMode = RenderMode::Translucent;
        item->CullingMode = CullingMode::Neither;
        item->BillboardMode = BillboardMode::None;
        item->Wireframe = false;
        item->Lighting = false;
        item->NoLines = false;
        item->Diffuse = color;
        item->Ambient = Vector3{};
        item->Specular = Vector3{};
        item->Emission = Vector3{};
        item->LightInfo = LightInfo::Zero;
        item->TexgenMode = TexgenMode::None;
        item->XRepeat = xRepeat;
        item->YRepeat = yRepeat;
        item->HasTexture = true;
        item->TextureBindingId = bindingId;
        item->TexcoordMatrix = RendererDetail::IdentityMatrix();
        item->Transform = RendererDetail::IdentityMatrix();
        item->MeshModel.reset();
        item->MeshObject.reset();
        MPHREAD_DEBUG_ASSERT(matrixStack.size() >= static_cast<std::size_t>(16 * matrixStackCount));
        item->MatrixStackCount = matrixStackCount;
        for (std::int32_t i = 0; i < 16 * matrixStackCount; ++i)
        {
            (*item->MatrixStack)[static_cast<std::size_t>(i)] = matrixStack[static_cast<std::size_t>(i)];
        }
        item->OverrideColor.reset();
        item->PaletteOverride.reset();
        item->Points = std::move(uvsAndVerts);
        item->ScaleS = scaleS;
        item->ScaleT = scaleT;
        item->ItemCount = segmentCount;
        AddRenderItem(item);
    }

    void Scene::AddRenderItem(const std::shared_ptr<MphRead::RenderItem>& item)
    {
        if (_collectingPreview)
        {
            _previewItems.push_back(item);
            _usedRenderItems.push_back(item);
            return;
        }
        if (item->RenderMode == RenderMode::Decal)
        {
            _decalItems.push_back(item);
        }
        else
        {
            _nonDecalItems.push_back(item);
        }
        if (item->RenderMode == RenderMode::Translucent || item->Alpha < 1.0F)
        {
            _translucentItems.push_back(item);
        }
        _usedRenderItems.push_back(item);
    }

    std::int32_t Scene::GetNextPolygonId()
    {
        return _nextPolygonId++;
    }

    RendererConcurrentQueue<std::shared_ptr<Entities::EntityBase>>& Scene::LoadedEntities() noexcept
    {
        return _loadedEntities;
    }

    bool Scene::InitEntities() const noexcept
    {
        return _initEntities;
    }

    void Scene::InitEntities(bool value) noexcept
    {
        _initEntities = value;
    }

    void Scene::InitLoadedEntity(std::int32_t count)
    {
        std::int32_t i = 0;
        while (count == -1 || i++ < count)
        {
            std::shared_ptr<Entities::EntityBase> entity;
            if (!_loadedEntities.TryDequeue(entity))
            {
                break;
            }
            InitializeEntity(entity);
            SceneSetup::LoadEntityResources(entity, this);
        }
    }

    void Scene::UpdateScene()
    {
        if (_playingLandingMovie)
        {
            return;
        }
        const auto main = Entities::PlayerEntity::Main();
        const bool playerActive = ((main->LoadFlags() & LoadFlags::Active) == LoadFlags::Active);
        if (!GameState::DialogPause())
        {
            if (playerActive)
            {
                main->UpdateTimedSounds();
                main->ProcessHudMessageQueue();
            }
            auto iterator = Entities();
            auto enumerator = iterator.GetEnumerator();
            while (enumerator.MoveNext())
            {
                auto entity = enumerator.Current();
                if (entity->Initialized && !entity->Process())
                {
                    SendMessage(Message::Destroyed, entity.get(), nullptr, 0, 0, 1);
                    entity->Destroy();
                    RemoveEntity(entity);
                }
            }
            Entities::PlayerEntity::PlayerAiData::UpdateVisibilityAndGlobals(*this);
            for (const auto& player : Entities::PlayerEntity::Players())
            {
                player->ClosestNode.reset();
            }
            for (const auto& player : Entities::PlayerEntity::Players())
            {
                if (player->IsBot() && player->Health() != 0)
                {
                    player->AiData->Process();
                }
            }
            if (playerActive)
            {
                main->ProcessModeHud();
            }
            GameState::UpdateFrame(this);
            GameState::UpdateState();
        }
        else if (GameState::SinglePlayer())
        {
            if (playerActive)
            {
                main->UpdateDialogs();
            }
            GameState::UpdateFrame(this);
        }
    }

    void Scene::GetDrawItems()
    {
        const auto main = Entities::PlayerEntity::Main();
        if (GameState::MenuPause())
        {
            const float simFrameTime = _frameTime;
            _frameTime = (1.0F / 60.0F) * static_cast<float>(_pendingEffectSteps);
            _pendingEffectSteps = 0;
            main->GetPauseMapRenderItems();
            _frameTime = simFrameTime;
            return;
        }
        if (_room)
        {
            _room->GetDrawInfo();
            _room->GetDisplayVolumes();
        }
        for (auto playerIt = GetPlayerEntities().GetEnumerator(); playerIt.MoveNext();)
        {
            auto player = playerIt.Current();
            if (!player->Initialized)
            {
                continue;
            }
            if (((player->LoadFlags() & LoadFlags::Active) == LoadFlags::Active))
            {
                player->Draw();
                player->GetDisplayVolumes();
            }
        }
        auto entities = Entities().GetEnumerator();
        while (entities.MoveNext())
        {
            auto entity = entities.Current();
            if (!entity->Initialized || entity->Type == EntityType::Player || entity->Type == EntityType::Room)
            {
                continue;
            }
            if (entity->ShouldDraw)
            {
                entity->GetDrawInfo();
            }
            if (_showVolumes != VolumeDisplay::None)
            {
                entity->GetDisplayVolumes();
            }
        }
        if (ProcessFrame() && GameState::MatchState() == MatchState::InProgress && !GameState::DialogPause())
        {
            for (std::int32_t i = 0; i < _pendingEffectSteps; ++i)
            {
                const std::uint64_t owed = static_cast<std::uint64_t>(_pendingEffectSteps - 1 - i);
                ProcessEffects(_effectFrame >= owed ? _effectFrame - owed : _effectFrame);
            }
        }
        _pendingEffectSteps = 0;
        for (const auto& element : _activeElements)
        {
            if (TypeExtensions::TestFlag(element->Flags, EffElemFlags::DrawEnabled))
            {
                for (const auto& particle : *element->Particles)
                {
                    Matrix4 matrix = _viewMatrix;
                    if (TypeExtensions::TestFlag(particle->Owner->Flags, EffElemFlags::UseTransform)
                        && !TypeExtensions::TestFlag(particle->Owner->Flags, EffElemFlags::UseMesh))
                    {
                        matrix = particle->Owner->Transform * matrix;
                    }
                    particle->InvokeSetVecsFunc(matrix);
                    particle->InvokeDrawFunc(1);
                    if (particle->ShouldDraw())
                    {
                        particle->AddRenderItem(this);
                    }
                }
            }
        }
        for (std::int32_t i = 0; i < _singleParticleCount; ++i)
        {
            _singleParticles[static_cast<std::size_t>(i)]->Process();
        }
        for (std::int32_t i = 0; i < _singleParticleCount; ++i)
        {
            auto single = _singleParticles[static_cast<std::size_t>(i)];
            if (single->ShouldDraw())
            {
                single->AddRenderItem(this);
            }
        }
        ModCollectPreview();
    }

    void Scene::UpdateUniforms()
    {
        UseRoomLights();
        // Every frame, as the lights: written only when the room was set, the
        // fog went to whichever program was bound then, and the scene program
        // kept the previous room's -- the launcher backdrop's warm Alinos fog
        // over a Data Shrine match on one renderer, its own green on the other.
        SetShaderFog();
        _shaderConstants->SetFogEnabled(_hasFog && FogOn());
        _shaderConstants->SetCelBands(Mods::RenderOptions::CelShading() ? Mods::RenderOptions::CelBands() : 0);
        _shaderConstants->SetShowColors(_showColors);
        if (ProcessFrame())
        {
            UpdateFade();
        }
    }

    void Scene::UseRoomLights()
    {
        _shaderConstants->Set(NativeRuntime::Rhi::SceneLightConstants{{
            {_light1Vector, _light1Color}, {_light2Vector, _light2Color}}});
    }

    void Scene::UseLight1(Vector3 vector, Vector3 color)
    {
        _shaderConstants->SetLight(0, NativeRuntime::Rhi::LightConstants{vector, color});
    }

    void Scene::UseLight2(Vector3 vector, Vector3 color)
    {
        _shaderConstants->SetLight(1, NativeRuntime::Rhi::LightConstants{vector, color});
    }

    void Scene::SetMatrixStack(const Matrix4& transform)
    {
        _shaderConstants->Set(NativeRuntime::Rhi::DrawConstants{
            std::span<const float>(&transform.M11, 16U)});
    }

    void Scene::SetFrameMatrices(const Matrix4& view, const Matrix4& projection)
    {
        _shaderConstants->Set(NativeRuntime::Rhi::FrameConstants{view, projection});
    }

    FadeType Scene::FadeType() const noexcept
    {
        return _fadeType;
    }

    void Scene::SetFade(MphRead::FadeType type, float length, bool overwrite, AfterFade afterFade, float delay)
    {
        if (!overwrite && _fadeType != MphRead::FadeType::None)
        {
            return;
        }
        _fadeType = type;
        _fadeDelay = delay;
        _fadePercent = 0.0F;
        if (type == MphRead::FadeType::None)
        {
            _fadeType = type;
            _fadeColor = 0.0F;
            _fadeIn = false;
            _fadeStart = 0.0F;
            _fadeLength = 0.0F;
        }
        else if (type == MphRead::FadeType::FadeInWhite)
        {
            _fadeColor = 1.0F;
            _fadeIn = true;
        }
        else if (type == MphRead::FadeType::FadeInBlack)
        {
            _fadeColor = 0.0F;
            _fadeIn = true;
        }
        else if (type == MphRead::FadeType::FadeOutWhite || type == MphRead::FadeType::FadeOutInWhite)
        {
            _fadeColor = 1.0F;
            _fadeIn = false;
        }
        else if (type == MphRead::FadeType::FadeOutBlack || type == MphRead::FadeType::FadeOutInBlack)
        {
            _fadeColor = 0.0F;
            _fadeIn = false;
        }
        _fadeStart = _globalElapsedTime;
        _fadeLength = length;
        _afterFade = afterFade;
        _fadeEnded = false;
    }

    void Scene::UpdateFade()
    {
        const Vector4 clearColor = _clearColor;
        (void)clearColor;
        if (_fadeType != MphRead::FadeType::None)
        {
            if (_fadeDelay > 0.0F)
            {
                _fadeDelay -= _frameTime * static_cast<float>(_pendingFadeSteps);
                _fadeStart = _globalElapsedTime;
            }
            _fadePercent = (_globalElapsedTime - _fadeStart) / _fadeLength;
            if (_fadePercent >= 1.0F)
            {
                _fadePercent = 1.0F;
                if (!_fadeEnded)
                {
                    EndFade();
                    _fadeEnded = true;
                }
            }
            else
            {
                _fadeEnded = false;
            }
            ModGoldenObserveFadeUpdate(_fadePercent);
        }
        else
        {
            _fadeEnded = false;
        }
        _pendingFadeSteps = 0;
    }

    void Scene::QuitGame(bool enteringShip)
    {
        _fadeType = MphRead::FadeType::None;
        DoCleanup();
        if (GameState::SinglePlayer())
        {
            Menu::NeededSave = enteringShip ? Menu::SaveFromShip : Menu::SaveFromExit;
            if (enteringShip)
            {
                RequireReference(::MphRead::GameState::StorySave).Health = RequireReference(::MphRead::GameState::StorySave).HealthMax;
                ManagedAt(RequireReference(::MphRead::GameState::StorySave).Ammo, 0) = ManagedAt(RequireReference(::MphRead::GameState::StorySave).AmmoMax, 0);
                ManagedAt(RequireReference(::MphRead::GameState::StorySave).Ammo, 1) = ManagedAt(RequireReference(::MphRead::GameState::StorySave).AmmoMax, 1);
            }
        }
        _close();
    }

    void Scene::DoCleanup()
    {
        if (!_exiting)
        {
            _exiting = true;
            if (_room)
            {
                _room->CancelTransition();
            }
            Entities::PlatformEntity::DestroyBeams();
            Entities::EnemyInstanceEntity::DestroyBeams();
            BreakEffectCycles();
            Sound::Sfx::ShutDown();
            OutputStop();
            if (const std::shared_ptr<std::stop_source> decoderCts = _decoderCts.load())
            {
                decoderCts->request_stop();
            }
            Selection::Clear();
        }
    }

    void Scene::ReleaseGpuResources()
    {
        if (Mods::Headless::Active())
        {
            return;
        }
        if (!SideScene())
        {
            ::MphRead::Mods::Render::LauncherHunter::NoteGlUnloaded();
        }
        // Every texture this scene created -- the models' texture/palette
        // pairs, HUD art, trails, the movie frames -- is in _ownedTextures.
        _texPalMap.clear();
        _ownedTextures.clear();
        _textureCopies.clear();
        _modelTextureSources.clear();
        _flatColors.clear();
        _gpuMeshCache.Clear();
        _transientGeometry.reset();
        _transientVertices.clear();
        // A launcher preview shares Read's model/particle/effect cache with
        // the running match. Releasing its GPU resources during a renderer
        // switch must not discard the definitions that future shots/bombs use.
        if (!SideScene()) Read::ClearCache();
        _celDepthView.reset();
        _celDepth.reset();
        _celColorView.reset();
        _celColor.reset();
        _sceneDepthStencilView.reset();
        _sceneDepthStencil.reset();
        _sceneColorView.reset();
        _sceneColor.reset();
        for (auto& sampler : _samplers)
        {
            sampler.reset();
        }
        // The command list owns the framebuffers built on those targets.
        _commands.reset();
        _pipelines.clear(); _recentPipelines = {};
        // The movie frames were owned textures, already released above.
        _topMovieBinding = -1;
        _botMovieBinding = -1;
        // The programs go with their shaders; the pipelines naming them went above.
        _shaderConstants = &_noShaderConstants;
        _sceneShaders.reset();
        // Everything above was retired, not destroyed: the GPU may still have
        // been reading it for a frame already submitted. This is the scene's
        // own context, so wait here and let the device destroy it now.
        if (_gpu != nullptr)
        {
            _gpu->WaitIdle();
            const NativeRuntime::Rhi::GpuResourceStatistics left = _gpu->Statistics();
            Mods::DebugLog::Line("gpu", "released a scene; the device still holds "
                + std::to_string(left.Textures) + " textures, " + std::to_string(left.Framebuffers)
                + " framebuffers, " + std::to_string(left.Shaders) + " shaders, "
                + std::to_string(left.Programs) + " programs, " + std::to_string(left.Retired) + " retired");
        }
    }

    void Scene::EndFade()
    {
        if (_afterFade == AfterFade::Exit || _afterFade == AfterFade::EnterShip)
        {
            QuitGame(_afterFade == AfterFade::EnterShip);
            return;
        }
        const AfterFade afterFade = _afterFade;
        if (afterFade == AfterFade::PlayMovie)
        {
            PlayMovie(_movieSettings.MovieId);
        }
        else if (afterFade == AfterFade::StopMovie)
        {
            if (_movieSettings.AfterPosition.has_value())
            {
                const Vector3 position = *_movieSettings.AfterPosition;
                const Vector3 facing = _movieSettings.AfterFacing.value_or(Entities::PlayerEntity::Main()->FacingVector());
                const auto newNodeRef = GetNodeRefByName("rmMain");
                Entities::PlayerEntity::Main()->Reposition(position, facing, newNodeRef);
            }
            StopMovie();
        }
        if (_fadeType == MphRead::FadeType::FadeOutInBlack)
        {
            SetFade(MphRead::FadeType::FadeInBlack, _fadeLength, true);
        }
        else if (_fadeType == MphRead::FadeType::FadeOutInWhite)
        {
            SetFade(MphRead::FadeType::FadeInWhite, _fadeLength, true);
        }
        else if (afterFade == AfterFade::LoadRoom)
        {
            MPHREAD_DEBUG_ASSERT(_room);
            _room->LoadRoom(false);
            const MphRead::FadeType fadeType = _fadeType == MphRead::FadeType::FadeOutWhite
                ? MphRead::FadeType::FadeInWhite : MphRead::FadeType::FadeInBlack;
            SetFade(fadeType, 10.0F / 30.0F, true);
        }
        else if (afterFade != AfterFade::PlayMovie && afterFade != AfterFade::StopMovie)
        {
            _fadeType = MphRead::FadeType::None;
            _fadeColor = 0.0F;
            _fadeIn = false;
            _fadePercent = 0.0F;
            _fadeStart = 0.0F;
            _fadeLength = 0.0F;
        }
    }

    void Scene::BeginTransient(TransientPrimitiveTopology topology)
    {
        if (!_transientGeometry)
        {
            throw ProgramException("Transient geometry resource is unavailable.");
        }
        _transientTopology = topology;
        _transientVertices.clear();
        _transientHasTexCoords = false;
    }

    void Scene::TransientVertex3(float x, float y, float z)
    {
        _transientVertices.push_back(TransientVertex{
            Vector3(x, y, z),
            _transientTexCoord
        });
    }

    void Scene::TransientVertex3(Vector3 vector)
    {
        _transientVertices.push_back(TransientVertex{
            vector,
            _transientTexCoord
        });
    }

    void Scene::TransientTexCoord3(float sCoord, float tCoord, float rCoord)
    {
        _transientTexCoord = Vector3(sCoord, tCoord, rCoord);
        _transientHasTexCoords = true;
    }

    void Scene::TransientTexCoord3(Vector3 coord)
    {
        _transientTexCoord = coord;
        _transientHasTexCoords = true;
    }

    void Scene::EndTransient()
    {
        if (!_transientGeometry)
        {
            throw ProgramException("Transient geometry resource is unavailable.");
        }
        _transientGeometry->Draw(_transientTopology,
            std::span<const TransientVertex>(
                _transientVertices.data(), _transientVertices.size()),
            _transientHasTexCoords);
        if (_transientHasTexCoords)
        {
            // Client arrays do not update fixed-function current texcoord.
            // The immediate path did, so explicitly retain its terminal state.
            _shaderConstants->SetInheritedTexCoord(_transientTexCoord);
        }
    }

    void Scene::RenderItem(const std::shared_ptr<MphRead::RenderItem>& item)
    {
        _shaderConstants->SelectMaterialOwner(item->MaterialOwnerId);
        struct ReturnMaterialOwner final
        {
            NativeRuntime::Rhi::ShaderConstantSink& Sink;
            ~ReturnMaterialOwner() { Sink.SelectMaterialOwner(0); }
        } returnMaterialOwner{*_shaderConstants};
        UseLight1(item->LightInfo.Light1Vector, item->LightInfo.Light1Color);
        UseLight2(item->LightInfo.Light2Vector, item->LightInfo.Light2Color);
        const ManagedArray<float>& matrixStack = RequireReference(item->MatrixStack);
        const std::int32_t matrixStackCapacity
            = static_cast<std::int32_t>(matrixStack.Length() / 16U);
        const std::int32_t matrixStackCount
            = std::clamp(item->MatrixStackCount, 0, matrixStackCapacity);
        if (matrixStackCount > 0)
        {
            _shaderConstants->Set(NativeRuntime::Rhi::DrawConstants{std::span<const float>(
                matrixStack.Data(), static_cast<std::size_t>(matrixStackCount) * 16U)});
        }
        else
        {
            SetMatrixStack(item->Transform);
        }
        Matrix4 viewInv = RendererDetail::IdentityMatrix();
        if (item->BillboardMode == BillboardMode::Sphere)
        {
            viewInv = _viewInvRotMatrix;
        }
        else if (item->BillboardMode == BillboardMode::Cylinder)
        {
            viewInv = _viewInvRotYMatrix;
        }
        _shaderConstants->SetBillboard(viewInv);
        DoMaterial(*item);
        DoTexture(*item);
        // With face culling switched off (the B key) nothing ever enables it.
        namespace Rhi = NativeRuntime::Rhi;
        Rhi::CullMode cull = Rhi::CullMode::None;
        if (_faceCulling && item->CullingMode == CullingMode::Back)
        {
            cull = Rhi::CullMode::Back;
        }
        else if (_faceCulling && item->CullingMode == CullingMode::Front)
        {
            cull = Rhi::CullMode::Front;
        }
        const bool wireframe = _wireframeLevel > 0 || item->Wireframe;
        Commands().SetPipeline(ScenePipeline(_itemPass, cull,
            wireframe ? Rhi::FillMode::Wireframe : Rhi::FillMode::Solid,
            wireframe ? std::max(1, _wireframeLevel) : 1));
        if (item->Type == RenderItemType::Mesh)
        {
            DrawGpuMesh(item->MeshModel, item->MeshObject);
        }
        else if (item->Type == RenderItemType::Box)
        {
            RenderBox(*item->Points);
        }
        else if (item->Type == RenderItemType::Cylinder)
        {
            RenderCylinder(*item->Points);
        }
        else if (item->Type == RenderItemType::Sphere)
        {
            RenderSphere(*item->Points);
        }
        else if (item->Type == RenderItemType::Quad)
        {
            RenderQuad(*item->Points);
        }
        else if (item->Type == RenderItemType::Ngon)
        {
            if (_volumeEdges != 1)
            {
                RenderNgon(*item->Points, item->ItemCount);
            }
            if (_volumeEdges != 2 && !item->NoLines)
            {
                RenderNgonLines(*item->Points, item->ItemCount);
            }
        }
        else if (item->Type == RenderItemType::Particle)
        {
            RenderParticle(*item);
        }
        else if (item->Type == RenderItemType::TrailSingle)
        {
            RenderTrailSingle(*item);
        }
        else if (item->Type == RenderItemType::TrailMulti)
        {
            RenderTrailMulti(*item);
        }
        else if (item->Type == RenderItemType::TrailStack)
        {
            RenderTrailStack(*item);
        }
    }

    void Scene::RenderBox(const ManagedArray<Vector3>& verts)
    {
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientVertex3(verts[2]); TransientVertex3(verts[6]); TransientVertex3(verts[0]); TransientVertex3(verts[4]);
        TransientVertex3(verts[1]); TransientVertex3(verts[5]); TransientVertex3(verts[3]); TransientVertex3(verts[7]);
        TransientVertex3(verts[2]); TransientVertex3(verts[6]);
        EndTransient();
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientVertex3(verts[5]); TransientVertex3(verts[4]); TransientVertex3(verts[7]); TransientVertex3(verts[6]);
        EndTransient();
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientVertex3(verts[3]); TransientVertex3(verts[2]); TransientVertex3(verts[1]); TransientVertex3(verts[0]);
        EndTransient();
    }

    void Scene::RenderCylinder(const ManagedArray<Vector3>& verts)
    {
        BeginTransient(TransientPrimitiveTopology::TriangleFan);
        TransientVertex3(verts[32]);
        for (std::int32_t i = 0; i < 16; ++i) TransientVertex3(verts[static_cast<std::size_t>(i)]);
        TransientVertex3(verts[0]);
        EndTransient();
        BeginTransient(TransientPrimitiveTopology::TriangleFan);
        TransientVertex3(verts[33]);
        for (std::int32_t i = 31; i >= 16; --i) TransientVertex3(verts[static_cast<std::size_t>(i)]);
        TransientVertex3(verts[31]);
        EndTransient();
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        for (std::int32_t i = 0; i < 16; ++i)
        {
            TransientVertex3(verts[static_cast<std::size_t>(i)]);
            TransientVertex3(verts[static_cast<std::size_t>(i + 16)]);
        }
        TransientVertex3(verts[0]);
        TransientVertex3(verts[16]);
        EndTransient();
    }

    void Scene::RenderSphere(const ManagedArray<Vector3>& verts)
    {
        const std::int32_t stackCount = DisplaySphereStacks;
        const std::int32_t sectorCount = DisplaySphereSectors;
        BeginTransient(TransientPrimitiveTopology::Triangles);
        for (std::int32_t i = 0; i < stackCount; ++i)
        {
            std::int32_t k1 = i * (sectorCount + 1);
            std::int32_t k2 = k1 + sectorCount + 1;
            for (std::int32_t j = 0; j < sectorCount; ++j, ++k1, ++k2)
            {
                if (i != 0)
                {
                    TransientVertex3(verts[static_cast<std::size_t>(k1 + 1)]);
                    TransientVertex3(verts[static_cast<std::size_t>(k2)]);
                    TransientVertex3(verts[static_cast<std::size_t>(k1)]);
                }
                if (i != stackCount - 1)
                {
                    TransientVertex3(verts[static_cast<std::size_t>(k2 + 1)]);
                    TransientVertex3(verts[static_cast<std::size_t>(k2)]);
                    TransientVertex3(verts[static_cast<std::size_t>(k1 + 1)]);
                }
            }
        }
        EndTransient();
    }

    void Scene::RenderQuad(const ManagedArray<Vector3>& verts)
    {
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientVertex3(verts[0]); TransientVertex3(verts[3]); TransientVertex3(verts[1]); TransientVertex3(verts[2]);
        EndTransient();
    }

    void Scene::RenderNgon(const ManagedArray<Vector3>& verts, std::int32_t count)
    {
        BeginTransient(TransientPrimitiveTopology::TriangleFan);
        for (std::int32_t i = 0; i < count; ++i) TransientVertex3(verts[static_cast<std::size_t>(i)]);
        EndTransient();
    }

    void Scene::RenderNgonLines(const ManagedArray<Vector3>& verts, std::int32_t count)
    {
        const Vector4 color = _showCollision && _colDisplayColor == CollisionColor::None && _colDisplayAlpha == 1.0F
            ? Vector4(0.0F, 0.0F, 1.0F, 1.0F) : Vector4(1.0F, 0.0F, 0.0F, 1.0F);
        _shaderConstants->SetOverrideColor(color);
        BeginTransient(TransientPrimitiveTopology::LineLoop);
        for (std::int32_t i = 0; i < count; ++i) TransientVertex3(verts[static_cast<std::size_t>(i)]);
        EndTransient();
    }

    void Scene::RenderParticle(const MphRead::RenderItem& item)
    {
        const auto& p = *item.Points;
        BeginTransient(TransientPrimitiveTopology::Quads);
        TransientTexCoord3(p[0].X * item.ScaleS, p[0].Y * item.ScaleT, 0.0F); TransientVertex3(p[1]);
        TransientTexCoord3(p[2].X * item.ScaleS, p[2].Y * item.ScaleT, 0.0F); TransientVertex3(p[3]);
        TransientTexCoord3(p[4].X * item.ScaleS, p[4].Y * item.ScaleT, 0.0F); TransientVertex3(p[5]);
        TransientTexCoord3(p[6].X * item.ScaleS, p[6].Y * item.ScaleT, 0.0F); TransientVertex3(p[7]);
        EndTransient();
    }

    void Scene::RenderTrailSingle(const MphRead::RenderItem& item)
    {
        const auto& p = *item.Points;
        BeginTransient(TransientPrimitiveTopology::QuadStrip);
        TransientTexCoord3(p[0]); TransientVertex3(p[1]);
        TransientTexCoord3(p[2]); TransientVertex3(p[3]);
        TransientTexCoord3(p[4]); TransientVertex3(p[5]);
        TransientTexCoord3(p[6]); TransientVertex3(p[7]);
        EndTransient();
    }

    void Scene::RenderTrailMulti(const MphRead::RenderItem& item)
    {
        MPHREAD_DEBUG_ASSERT(item.ItemCount >= 4 && item.ItemCount % 2 == 0);
        BeginTransient(TransientPrimitiveTopology::QuadStrip);
        for (std::int32_t i = 0; i < item.ItemCount; i += 2)
        {
            TransientTexCoord3((*item.Points)[static_cast<std::size_t>(i)]);
            TransientVertex3((*item.Points)[static_cast<std::size_t>(i + 1)]);
        }
        EndTransient();
    }

    void Scene::RenderTrailStack(const MphRead::RenderItem& item)
    {
        for (std::int32_t i = 0; i < item.ItemCount; ++i)
        {
            const std::size_t base = static_cast<std::size_t>(i * 8);
            BeginTransient(TransientPrimitiveTopology::Quads);
            TransientTexCoord3((*item.Points)[base]); TransientVertex3((*item.Points)[base + 1]);
            TransientTexCoord3((*item.Points)[base + 2]); TransientVertex3((*item.Points)[base + 3]);
            TransientTexCoord3((*item.Points)[base + 4]); TransientVertex3((*item.Points)[base + 5]);
            TransientTexCoord3((*item.Points)[base + 6]); TransientVertex3((*item.Points)[base + 7]);
            EndTransient();
        }
    }

    void Scene::SetPauseMenuUniforms()
    {
        if (GameState::MenuPause() && _cameraMode == MphRead::CameraMode::Player)
        {
            const auto matrices = Entities::PlayerEntity::Main()->GetPauseMapMatrices();
            SetFrameMatrices(matrices.first, matrices.second);
        }
    }

    std::shared_ptr<LayerInfo> Scene::Layer1Info() const noexcept { return _layer1Info; }
    std::shared_ptr<LayerInfo> Scene::Layer2Info() const noexcept { return _layer2Info; }
    std::shared_ptr<LayerInfo> Scene::Layer3Info() const noexcept { return _layer3Info; }
    std::shared_ptr<LayerInfo> Scene::Layer4Info() const noexcept { return _layer4Info; }
    std::shared_ptr<LayerInfo> Scene::Layer5Info() const noexcept { return _layer5Info; }

    void Scene::SetHudLayerUniforms()
    {
        Commands().SetPipeline(ScenePipeline(ScenePass::HudModel,
            _faceCulling ? NativeRuntime::Rhi::CullMode::Back : NativeRuntime::Rhi::CullMode::None,
            NativeRuntime::Rhi::FillMode::Solid, 1));
        const Matrix4 identity = RendererDetail::IdentityMatrix();
        SetMatrixStack(identity);
        _shaderConstants->SetBillboard(identity);
        const Vector3 one(1.0F, 1.0F, 1.0F);
        _shaderConstants->SetInheritedColor(Vector4(one, 1.0F));
        _shaderConstants->SetSurface(NativeRuntime::Rhi::MaterialConstants{false, one, one, one, one, 1.0F,
            static_cast<std::int32_t>(PolygonMode::Modulate)});
        _shaderConstants->SetTexgen(static_cast<std::int32_t>(TexgenMode::None), identity);
        _shaderConstants->SetUseTexture(true);
        _shaderConstants->SetOverride(nullptr);
        _shaderConstants->SetPaletteOverride(nullptr);
        _shaderConstants->SetFogEnabled(false);
        _shaderConstants->SetFlatColor(nullptr);
        _shaderConstants->SetCelBands(0);
        const Matrix4 orthoMatrix = Matrix4::CreateOrthographic(
            static_cast<float>(_rendererSize.X), static_cast<float>(_rendererSize.Y), 0.5F, 1.5F);
        SetFrameMatrices(identity, orthoMatrix);
    }

    void Scene::UnsetHudLayerUniforms()
    {
        SetFrameMatrices(_viewMatrix, _perspectiveMatrix);
    }

    void Scene::DrawHudLayer(const std::shared_ptr<LayerInfo>& info)
    {
        if (info->BindingId == -1)
        {
            return;
        }
        _shaderConstants->SetLayerAlpha(info->Alpha);
        BindSceneTexture(0, info->BindingId, SamplerFor(false, RepeatMode::Clamp, RepeatMode::Clamp));
        const float viewWidth = static_cast<float>(_rendererSize.X);
        const float viewHeight = static_cast<float>(_rendererSize.Y);
        float width;
        float height;
        if (info->ScaleX == -1.0F || info->ScaleY == -1.0F)
        {
            const float size = ::MphRead::NativeRuntime::MathMax(viewWidth, viewHeight) / 2.0F;
            width = size / (viewWidth / 2.0F);
            height = size / (viewHeight / 2.0F);
        }
        else
        {
            width = viewWidth * info->ScaleX / 2.0F / (viewWidth / 2.0F);
            height = viewHeight * info->ScaleY / 2.0F / (viewHeight / 2.0F);
        }
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientTexCoord3(1,0,0); TransientVertex3(width + info->ShiftX, height + info->ShiftY, 0);
        TransientTexCoord3(0,0,0); TransientVertex3(-width + info->ShiftX, height + info->ShiftY, 0);
        TransientTexCoord3(1,1,0); TransientVertex3(width + info->ShiftX, -height + info->ShiftY, 0);
        TransientTexCoord3(0,1,0); TransientVertex3(-width + info->ShiftX, -height + info->ShiftY, 0);
        EndTransient();
        UnbindSceneTexture(0);
    }

    void Scene::DrawCustomCrosshair(Vector3 color, float posX, float posY)
    {
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float offX = posX * 2.0F - 1.0F;
        const float offY = 1.0F - posY * 2.0F;
        const auto style = Mods::Render::Crosshair::Style;
        const float scale = Mods::Render::Crosshair::Scale();
        _shaderConstants->SetFadeColor(Vector4(color.X, color.Y, color.Z, 1.0F));
        const auto bars = Mods::Render::Crosshair::BarsOf(style, scale);
        for (const auto& bar : bars)
        {
            const auto edges = Mods::Render::Crosshair::EdgesOf(bar);
            const float left = std::get<0>(edges);
            const float right = std::get<1>(edges);
            const float bottom = std::get<2>(edges);
            const float top = std::get<3>(edges);
            BeginTransient(TransientPrimitiveTopology::TriangleStrip);
            TransientVertex3(offX + right / halfW, offY + top / halfH, 0);
            TransientVertex3(offX + left / halfW, offY + top / halfH, 0);
            TransientVertex3(offX + right / halfW, offY + bottom / halfH, 0);
            TransientVertex3(offX + left / halfW, offY + bottom / halfH, 0);
            EndTransient();
        }
        const auto ring = Mods::Render::Crosshair::RingOf(style, scale);
        const float radius = std::get<0>(ring);
        const float thickness = std::get<1>(ring);
        if (thickness > 0.0F)
        {
            constexpr std::int32_t segments = 40;
            const float inner = radius - thickness / 2.0F;
            const float outer = radius + thickness / 2.0F;
            BeginTransient(TransientPrimitiveTopology::TriangleStrip);
            for (std::int32_t i = 0; i <= segments; ++i)
            {
                const float angle = TwoPi * static_cast<float>(i) / static_cast<float>(segments);
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                TransientVertex3(offX + outer * c / halfW, offY + outer * s / halfH, 0);
                TransientVertex3(offX + inner * c / halfW, offY + inner * s / halfH, 0);
            }
            EndTransient();
        }
        _shaderConstants->SetFadeColor(Vector4{});
    }

    void Scene::DrawHitMarker(Vector4 color, float posX, float posY)
    {
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float offX = posX * 2.0F - 1.0F;
        const float offY = 1.0F - posY * 2.0F;
        const float scale = Mods::Render::Crosshair::Scale();
        constexpr float gap = 4.0F;
        constexpr float length = 7.0F;
        constexpr float thickness = 2.0F;
        const float diagonal = std::sqrt(0.5F);
        _shaderConstants->SetFadeColor(color);
        for (std::int32_t i = 0; i < 4; ++i)
        {
            const float dx = ((i & 1) == 0 ? -1.0F : 1.0F) * diagonal;
            const float dy = ((i & 2) == 0 ? -1.0F : 1.0F) * diagonal;
            const float x0 = dx * gap * scale;
            const float y0 = dy * gap * scale;
            const float x1 = dx * (gap + length) * scale;
            const float y1 = dy * (gap + length) * scale;
            const float hx = -dy * thickness * scale / 2.0F;
            const float hy = dx * thickness * scale / 2.0F;
            BeginTransient(TransientPrimitiveTopology::TriangleStrip);
            TransientVertex3(offX + (x0 + hx) / halfW, offY + (y0 + hy) / halfH, 0);
            TransientVertex3(offX + (x0 - hx) / halfW, offY + (y0 - hy) / halfH, 0);
            TransientVertex3(offX + (x1 + hx) / halfW, offY + (y1 + hy) / halfH, 0);
            TransientVertex3(offX + (x1 - hx) / halfW, offY + (y1 - hy) / halfH, 0);
            EndTransient();
        }
        _shaderConstants->SetFadeColor(Vector4{});
    }

    // A bound texture over a HUD rectangle, in the 256x192 space. Linear: a
    // photograph reduced to a thumbnail has no DS pixel grid to preserve.
    void Scene::DrawHudTexture(float left, float top, float right, float bottom, std::int32_t bindingId,
        float alpha, bool smooth)
    {
        if (bindingId <= 0)
        {
            return;
        }
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float x0 = (left / 256.0F * _rendererSize.X - halfW) / halfW;
        const float x1 = (right / 256.0F * _rendererSize.X - halfW) / halfW;
        const float y0 = (halfH - top / 192.0F * _rendererSize.Y) / halfH;
        const float y1 = (halfH - bottom / 192.0F * _rendererSize.Y) / halfH;
        _shaderConstants->SetLayerAlpha(alpha);
        _shaderConstants->SetUseMask(false);
        BindSceneTexture(0, bindingId, SamplerFor(smooth, RepeatMode::Clamp, RepeatMode::Clamp));
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientTexCoord3(1.0F, 0.0F, 0.0F);
        TransientVertex3(x1, y0, 0);
        TransientTexCoord3(0.0F, 0.0F, 0.0F);
        TransientVertex3(x0, y0, 0);
        TransientTexCoord3(1.0F, 1.0F, 0.0F);
        TransientVertex3(x1, y1, 0);
        TransientTexCoord3(0.0F, 1.0F, 0.0F);
        TransientVertex3(x0, y1, 0);
        EndTransient();
        UnbindSceneTexture(0);
        _shaderConstants->SetLayerAlpha(1.0F);
    }

    // A filled circle; localCenter is a pixel offset from (posX, posY), y up.
    void Scene::DrawFlatDisc(float posX, float posY, OpenTK::Mathematics::Vector2 localCenter, float radius,
        Vector4 color, std::int32_t segments)
    {
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float offX = posX * 2.0F - 1.0F + localCenter.X / halfW;
        const float offY = 1.0F - posY * 2.0F + localCenter.Y / halfH;
        _shaderConstants->SetFadeColor(color);
        BeginTransient(TransientPrimitiveTopology::TriangleFan);
        TransientVertex3(offX, offY, 0);
        for (std::int32_t i = 0; i <= segments; i++)
        {
            const float angle = ::OpenTK::Mathematics::MathHelper::Pi * 2 * static_cast<float>(i) / static_cast<float>(segments);
            TransientVertex3(offX + radius * std::cos(angle) / halfW, offY + radius * std::sin(angle) / halfH, 0);
        }
        EndTransient();
        _shaderConstants->SetFadeColor(Vector4{});
    }

    // An unfilled ring; radius is to the middle of the stroke.
    void Scene::DrawFlatRing(float posX, float posY, OpenTK::Mathematics::Vector2 localCenter, float radius,
        float thickness, Vector4 color, std::int32_t segments)
    {
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float offX = posX * 2.0F - 1.0F + localCenter.X / halfW;
        const float offY = 1.0F - posY * 2.0F + localCenter.Y / halfH;
        const float inner = radius - thickness / 2;
        const float outer = radius + thickness / 2;
        _shaderConstants->SetFadeColor(color);
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        for (std::int32_t i = 0; i <= segments; i++)
        {
            const float angle = ::OpenTK::Mathematics::MathHelper::Pi * 2 * static_cast<float>(i) / static_cast<float>(segments);
            const float cos = std::cos(angle);
            const float sin = std::sin(angle);
            TransientVertex3(offX + outer * cos / halfW, offY + outer * sin / halfH, 0);
            TransientVertex3(offX + inner * cos / halfW, offY + inner * sin / halfH, 0);
        }
        EndTransient();
        _shaderConstants->SetFadeColor(Vector4{});
    }

    // A straight bar between two points local to (posX, posY), in pixels, y up.
    void Scene::DrawFlatLine(float posX, float posY, OpenTK::Mathematics::Vector2 from, OpenTK::Mathematics::Vector2 to,
        float thickness, Vector4 color)
    {
        const float dirX = to.X - from.X;
        const float dirY = to.Y - from.Y;
        const float len = std::sqrt(dirX * dirX + dirY * dirY);
        if (len < 0.0001F)
        {
            return;
        }
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float offX = posX * 2.0F - 1.0F;
        const float offY = 1.0F - posY * 2.0F;
        const float perpX = -dirY / len * (thickness / 2.0F);
        const float perpY = dirX / len * (thickness / 2.0F);
        _shaderConstants->SetFadeColor(color);
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientVertex3(offX + (from.X + perpX) / halfW, offY + (from.Y + perpY) / halfH, 0);
        TransientVertex3(offX + (from.X - perpX) / halfW, offY + (from.Y - perpY) / halfH, 0);
        TransientVertex3(offX + (to.X + perpX) / halfW, offY + (to.Y + perpY) / halfH, 0);
        TransientVertex3(offX + (to.X - perpX) / halfW, offY + (to.Y - perpY) / halfH, 0);
        EndTransient();
        _shaderConstants->SetFadeColor(Vector4{});
    }

    // A filled, axis-aligned square local to (posX, posY), in pixels.
    void Scene::DrawFlatSquare(float posX, float posY, OpenTK::Mathematics::Vector2 localCenter, float halfSize,
        Vector4 color)
    {
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float offX = posX * 2.0F - 1.0F;
        const float offY = 1.0F - posY * 2.0F;
        const float cx = localCenter.X;
        const float cy = localCenter.Y;
        _shaderConstants->SetFadeColor(color);
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientVertex3(offX + (cx + halfSize) / halfW, offY + (cy + halfSize) / halfH, 0);
        TransientVertex3(offX + (cx - halfSize) / halfW, offY + (cy + halfSize) / halfH, 0);
        TransientVertex3(offX + (cx + halfSize) / halfW, offY + (cy - halfSize) / halfH, 0);
        TransientVertex3(offX + (cx - halfSize) / halfW, offY + (cy - halfSize) / halfH, 0);
        EndTransient();
        _shaderConstants->SetFadeColor(Vector4{});
    }

    // A filled convex polygon, points local to localCenter and already in fan order.
    void Scene::DrawFlatPolygon(float posX, float posY, OpenTK::Mathematics::Vector2 localCenter,
        std::span<const OpenTK::Mathematics::Vector2> localPoints, Vector4 color)
    {
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float offX = posX * 2.0F - 1.0F;
        const float offY = 1.0F - posY * 2.0F;
        _shaderConstants->SetFadeColor(color);
        BeginTransient(TransientPrimitiveTopology::TriangleFan);
        for (const OpenTK::Mathematics::Vector2& point : localPoints)
        {
            const float x = localCenter.X + point.X;
            const float y = localCenter.Y + point.Y;
            TransientVertex3(offX + x / halfW, offY + y / halfH, 0);
        }
        EndTransient();
        _shaderConstants->SetFadeColor(Vector4{});
    }

    void Scene::DrawHudFlatBox(float left, float top, float right, float bottom, Vector4 color)
    {
        const float halfW = _rendererSize.X / 2.0F;
        const float halfH = _rendererSize.Y / 2.0F;
        const float x0 = (left / 256.0F * _rendererSize.X - halfW) / halfW;
        const float x1 = (right / 256.0F * _rendererSize.X - halfW) / halfW;
        const float y0 = (halfH - top / 192.0F * _rendererSize.Y) / halfH;
        const float y1 = (halfH - bottom / 192.0F * _rendererSize.Y) / halfH;
        _shaderConstants->SetFadeColor(color);
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientVertex3(x1, y0, 0); TransientVertex3(x0, y0, 0); TransientVertex3(x1, y1, 0); TransientVertex3(x0, y1, 0);
        EndTransient();
        _shaderConstants->SetFadeColor(Vector4{});
    }

    void Scene::DrawHudObject(const std::shared_ptr<HudObjectInstance>& inst, std::int32_t mode, float scale)
    {
        if (!inst->Enabled)
        {
            return;
        }
        float x = inst->PositionX;
        float y = inst->PositionY;
        float width = static_cast<float>(inst->Width);
        float height = static_cast<float>(inst->Height);
        const bool center = inst->Center;
        _shaderConstants->SetLayerAlpha(inst->Alpha);
        _shaderConstants->SetUseMask(inst->UseMask);
        BindSceneTexture(0, inst->BindingId, SamplerFor(inst->Smooth, RepeatMode::Clamp, RepeatMode::Clamp));
        const float viewWidth = static_cast<float>(_rendererSize.X);
        const float viewHeight = static_cast<float>(_rendererSize.Y);
        if (mode == 2)
        {
            width = width / 256.0F * viewWidth;
            height = height / 192.0F * viewHeight;
        }
        else if (mode == 1)
        {
            const float aspect = height / width;
            height = height / 192.0F * viewHeight;
            width = height / aspect;
        }
        else
        {
            const float aspect = width / height;
            width = width / 256.0F * viewWidth;
            height = width / aspect;
        }
        if (scale != 1.0F)
        {
            width *= scale;
            height *= scale;
        }
        const float viewLeft = -viewWidth / 2.0F;
        const float viewTop = viewHeight / 2.0F;
        float leftPos = viewLeft + x * viewWidth - (center ? width / 2.0F : 0.0F);
        float rightPos = leftPos + width;
        float topPos = viewTop - y * viewHeight + (center ? height / 2.0F : 0.0F);
        float bottomPos = topPos - height;
        leftPos /= viewWidth / 2.0F;
        rightPos /= viewWidth / 2.0F;
        topPos /= viewHeight / 2.0F;
        bottomPos /= viewHeight / 2.0F;
        if (inst->FlipHorizontal)
        {
            std::swap(rightPos, leftPos);
        }
        if (inst->FlipVertical)
        {
            std::swap(bottomPos, topPos);
        }
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientTexCoord3(1,0,0); TransientVertex3(rightPos, topPos, 0);
        TransientTexCoord3(0,0,0); TransientVertex3(leftPos, topPos, 0);
        TransientTexCoord3(1,1,0); TransientVertex3(rightPos, bottomPos, 0);
        TransientTexCoord3(0,1,0); TransientVertex3(leftPos, bottomPos, 0);
        EndTransient();
        UnbindSceneTexture(0);
    }

    void Scene::DrawIconModel(Vector2 position, float angle, const std::shared_ptr<ModelInstance>& inst,
        ColorRgb color, float alpha, float scaleMult)
    {
        const float scale = _rendererSize.Y / 192.0F * scaleMult;
        const Vector3 position3d(
            position.X * _rendererSize.X - static_cast<float>(_rendererSize.X / 2),
            (1.0F - position.Y) * _rendererSize.Y - static_cast<float>(_rendererSize.Y / 2), -1.0F);
        Matrix4 transform = CreateRotationZ(DegreesToRadians(angle))
            * CreateScale(scale, scale, 1.0F) * CreateTranslation(position3d);
        SetMatrixStack(transform);
        const auto model = inst->Model();
        UpdateMaterials(model, 0);
        _shaderConstants->SetMaterialAlpha(alpha);
        BindSceneTexture(0, model->Materials->at(0)->TextureBindingId, SamplerFor(false, RepeatMode::Clamp, RepeatMode::Clamp));
        _shaderConstants->SetInheritedColor(Vector4(color.Red / 31.0F, color.Green / 31.0F, color.Blue / 31.0F, 1.0F));
        DrawGpuMesh(model, model->Meshes->at(0));
        UnbindSceneTexture(0);
        SetMatrixStack(RendererDetail::IdentityMatrix());
    }

    void Scene::DrawHudFilterModel(const std::shared_ptr<ModelInstance>& inst, float alpha)
    {
        const auto model = inst->Model();
        UpdateMaterials(model, 0);
        Material& material = *model->Materials->at(0);
        _shaderConstants->SetMaterialAlpha(material.Alpha / 31.0F * alpha);
        BindSceneTexture(0, material.TextureBindingId, SamplerFor(false, RepeatMode::Clamp, RepeatMode::Clamp));
        const float viewWidth = static_cast<float>(_rendererSize.X);
        const float viewHeight = static_cast<float>(_rendererSize.Y);
        BeginTransient(TransientPrimitiveTopology::TriangleStrip);
        TransientTexCoord3(1,0,0); TransientVertex3(viewWidth, viewHeight, -1);
        TransientTexCoord3(0,0,0); TransientVertex3(-viewWidth, viewHeight, -1);
        TransientTexCoord3(1,1,0); TransientVertex3(viewWidth, -viewHeight, -1);
        TransientTexCoord3(0,1,0); TransientVertex3(-viewWidth, -viewHeight, -1);
        EndTransient();
        UnbindSceneTexture(0);
    }

    void Scene::DrawHudDamageModel(const std::shared_ptr<ModelInstance>& inst)
    {
        const auto model = inst->Model();
        UpdateMaterials(model, 0);
        _shaderConstants->SetMaterialAlpha(1.0F);
        BindSceneTexture(0, model->Materials->at(0)->TextureBindingId, SamplerFor(false, RepeatMode::Clamp, RepeatMode::Clamp));
        const float viewWidth = static_cast<float>(_rendererSize.X);
        const float viewHeight = static_cast<float>(_rendererSize.Y);
        const float xOffset = -viewWidth / 2.0F;
        const float yOffset = -viewHeight / 2.0F;
        for (std::int32_t i = 1; i < 9; ++i)
        {
            Node& node = *model->Nodes->at(static_cast<std::size_t>(i));
            if (node.Enabled)
            {
                const float width = node.MaxBounds.X - node.MinBounds.X;
                const float height = node.MaxBounds.Y - node.MinBounds.Y;
                float newWidth = width / 256.0F * viewWidth;
                float newHeight = height / 192.0F * viewHeight;
                newWidth *= model->Scale.X;
                newHeight *= model->Scale.Y;
                Matrix4 transform = CreateScale(newWidth / width, newHeight / height, 1.0F);
                transform.M41 = xOffset;
                transform.M42 = yOffset;
                transform.M43 = -1.0F;
                node.Animation = transform;
            }
        }
        model->UpdateMatrixStack();
        const auto& values = *model->MatrixStackValues;
        const std::size_t copyCount = std::min(values.Length(), _hudMatrixStack.size());
        for (std::size_t i = 0; i < copyCount; ++i)
        {
            _hudMatrixStack[i] = values[i];
        }
        const std::int32_t matrixCount = static_cast<std::int32_t>(
            std::min(model->NodeMatrixIds->size(), _hudMatrixStack.size() / 16U));
        _shaderConstants->Set(NativeRuntime::Rhi::DrawConstants{std::span<const float>(
            _hudMatrixStack.data(), static_cast<std::size_t>(matrixCount) * 16U)});
        for (std::int32_t i = 1; i < 9; ++i)
        {
            const Node& node = *model->Nodes->at(static_cast<std::size_t>(i));
            if (node.Enabled)
            {
                const std::shared_ptr<Mesh>& mesh
                    = model->Meshes->at(static_cast<std::size_t>(node.MeshId / 2));
                DrawGpuMesh(model, mesh);
            }
        }
        UnbindSceneTexture(0);
        SetMatrixStack(RendererDetail::IdentityMatrix());
    }

    void Scene::DoMaterial(const MphRead::RenderItem& item)
    {
        const NativeRuntime::FrameTelemetry::Scope measured(NativeRuntime::FrameTelemetry::Phase::Material);
        _shaderConstants->SetInheritedColor(Vector4(item.Diffuse, 1.0F));
        _shaderConstants->Set(NativeRuntime::Rhi::MaterialConstants{
            LightingOn() && item.Lighting, item.Diffuse, item.Ambient, item.Specular,
            item.Emission, item.Alpha, static_cast<std::int32_t>(item.PolygonMode)});
    }

    void Scene::DoTexture(const MphRead::RenderItem& item)
    {
        if (item.HasTexture)
        {
            BindSceneTexture(0, item.TextureBindingId, SamplerFor(FilteringOn(), item.XRepeat, item.YRepeat));
            _shaderConstants->SetTexgen(static_cast<std::int32_t>(item.TexgenMode), item.TexcoordMatrix);
        }
        _shaderConstants->SetUseTexture(item.HasTexture && _showTextures);
        SetFlatColor(item.HasTexture && _showTextures ? item.TextureBindingId : -1);
        _shaderConstants->SetOverride(item.OverrideColor.has_value() ? &*item.OverrideColor : nullptr);
        _shaderConstants->SetPaletteOverride(item.PaletteOverride.has_value() ? &*item.PaletteOverride : nullptr);
    }

    void Scene::SetFlatColor(std::int32_t bindingId)
    {
        auto found = _flatColors.find(bindingId);
        if (Mods::RenderOptions::CelShading() && bindingId != -1 && found != _flatColors.end())
        {
            _shaderConstants->SetFlatColor(&found->second);
        }
        else
        {
            _shaderConstants->SetFlatColor(nullptr);
        }
    }

    void Scene::LookAt(Vector3 target)
    {
        _cameraMode = MphRead::CameraMode::Roam;
        _inputMode = InputMode::CameraOnly;
        _cameraPosition = Vector3(target.X, target.Y, target.Z + 5.0F);
        _cameraFacing = Vector3(0.0F, 0.0F, -1.0F);
        _cameraUp = Vector3(0.0F, 1.0F, 0.0F);
        _cameraRight = Vector3(1.0F, 0.0F, 0.0F);
    }

    bool Scene::IsFreeCam() const noexcept { return _freeCam; }

    bool Scene::ScoreboardOverFreeCamera() const
    {
        const auto main = Entities::PlayerEntity::Main();
        return Mods::SpectatorMode::FreeCamera() && Mods::SpectatorMode::ShowScoreboard()
            && ((main->LoadFlags() & LoadFlags::Active) == LoadFlags::Active);
    }

    void Scene::SetFreeCamera(bool on)
    {
        if (on == _freeCam)
        {
            return;
        }
        if (!on)
        {
            _cameraMode = MphRead::CameraMode::Player;
            _inputMode = InputMode::All;
            _freeCam = false;
            Mods::SpectatorMode::NoteFreeCamera(false);
            return;
        }
        const auto main = Entities::PlayerEntity::Main();
        _cameraPosition = RequireReference(main->CameraInfo()).Position;
        _cameraFacing = RequireReference(main->CameraInfo()).Facing;
        if (LengthSquared(_cameraFacing) < 0.0001F)
        {
            _cameraFacing = Vector3(0.0F, 0.0F, -1.0F);
        }
        _cameraFacing = _cameraFacing.Normalized();
        _cameraRight = Vector3::Cross(_cameraFacing, Vector3(0.0F, 1.0F, 0.0F));
        _cameraUp = Vector3::Cross(_cameraRight, _cameraFacing);
        _cameraMode = MphRead::CameraMode::Roam;
        _inputMode = InputMode::CameraOnly;
        _freeCam = true;
        Mods::SpectatorMode::NoteFreeCamera(true);
    }

    void Scene::ToggleFreeCamera()
    {
        SetFreeCamera(!_freeCam);
    }

    void Scene::OnMouseClick(bool down)
    {
        if (_inputMode != InputMode::PlayerOnly)
        {
            _leftMouse = down;
        }
    }

    void Scene::OnMouseMove(float deltaX, float deltaY)
    {
        if ((_leftMouse || _freeCam) && AllowCameraMovement() && _inputMode != InputMode::PlayerOnly)
        {
            float sensitivity = 1.0F;
            float invertX = 1.0F;
            float invertY = 1.0F;
            if (_freeCam)
            {
                sensitivity = Mods::InputSettings::MouseSensitivity();
                invertX = Mods::InputSettings::InvertMouseX() ? -1.0F : 1.0F;
                invertY = Mods::InputSettings::InvertMouseY() ? -1.0F : 1.0F;
            }
            const float moveX = deltaX * sensitivity * invertX;
            const float moveY = deltaY * sensitivity * invertY;
            if (_cameraMode == MphRead::CameraMode::Pivot)
            {
                _pivotAngleX += moveY / 1.5F;
                _pivotAngleX = std::clamp(_pivotAngleX, -90.0F, 90.0F);
                _pivotAngleY += moveX / 1.5F;
                _pivotAngleY = std::fmod(_pivotAngleY, 360.0F);
            }
            else if (_cameraMode == MphRead::CameraMode::Roam && _freeCam)
            {
                // Applied on the next simulation step (MoveRoamCamera).
                _roamMouseX += moveX;
                _roamMouseY += moveY;
            }
            else if (_cameraMode == MphRead::CameraMode::Roam)
            {
                UpdateCameraRotation(DegreesToRadians(moveX / 1.5F), DegreesToRadians(-moveY / 1.5F));
            }
        }
    }

    void Scene::OnMouseWheel(float offsetY)
    {
        if (_cameraMode == MphRead::CameraMode::Pivot && AllowCameraMovement()
            && _inputMode != InputMode::PlayerOnly)
        {
            _pivotDistance += offsetY / -1.5F;
            if (_pivotDistance < 0.0F)
            {
                _pivotDistance = 0.0F;
            }
            else if (_pivotDistance > 1000.0F)
            {
                _pivotDistance = 1000.0F;
            }
        }
    }

    bool Scene::ShowCollision() const noexcept { return _showCollision; }
    EntityType Scene::ColEntDisplay() const noexcept { return _colEntDisplay; }
    Terrain Scene::ColTerDisplay() const noexcept { return _colTerDisplay; }
    CollisionType Scene::ColTypeDisplay() const noexcept { return _colTypeDisplay; }
    CollisionColor Scene::ColDisplayColor() const noexcept { return _colDisplayColor; }
    float Scene::ColDisplayAlpha() const noexcept { return _colDisplayAlpha; }

    void Scene::OnKeyHeld()
    {
        using RendererPlatform::Key;
        if (_keyboardState->IsKeyDown(RendererPlatform::LeftAltKey) || _keyboardState->IsKeyDown(RendererPlatform::RightAltKey))
        {
            Selection::OnKeyHeld(*_keyboardState);
            return;
        }
        if (!AllowCameraMovement() || _inputMode == InputMode::PlayerOnly)
        {
            return;
        }
        if (_cameraMode == MphRead::CameraMode::Pivot)
        {
            const bool shift = _keyboardState->IsKeyDown(Key::LeftShift) || _keyboardState->IsKeyDown(RendererPlatform::RightShiftKey);
            const float rotStep = shift ? -3.0F : -1.5F;
            if (_keyboardState->IsKeyDown(Key::Up))
            {
                _pivotAngleX += rotStep;
                _pivotAngleX = std::clamp(_pivotAngleX, -90.0F, 90.0F);
            }
            else if (_keyboardState->IsKeyDown(Key::Down))
            {
                _pivotAngleX -= rotStep;
                _pivotAngleX = std::clamp(_pivotAngleX, -90.0F, 90.0F);
            }
            if (_keyboardState->IsKeyDown(Key::Left))
            {
                _pivotAngleY += rotStep;
                _pivotAngleY = std::fmod(_pivotAngleY, 360.0F);
            }
            else if (_keyboardState->IsKeyDown(Key::Right))
            {
                _pivotAngleY -= rotStep;
                _pivotAngleY = std::fmod(_pivotAngleY, 360.0F);
            }
        }
    }

    // The free camera moves once per 60 Hz simulation step, mouse look
    // included: the world it looks at changes at that rate, and a camera that
    // slides between steps makes every moving thing in view judder against it.
    void Scene::MoveRoamCamera()
    {
        using RendererPlatform::Key;
        const float mouseX = _roamMouseX, mouseY = _roamMouseY;
        _roamMouseX = _roamMouseY = 0.0F;
        if (_cameraMode != MphRead::CameraMode::Roam || !AllowCameraMovement() || _inputMode == InputMode::PlayerOnly
            || Mods::PauseMenu::Open() || Mods::Chat::ChatBox::Composing())
        {
            return;
        }
        constexpr float steps = 1.0F;
        const bool keys = !(_keyboardState->IsKeyDown(RendererPlatform::LeftAltKey) || _keyboardState->IsKeyDown(RendererPlatform::RightAltKey));
        const bool shift = keys && (_keyboardState->IsKeyDown(Key::LeftShift) || _keyboardState->IsKeyDown(RendererPlatform::RightShiftKey));
        // The death camera's speed (PlayerEntity::UpdateCameraFree: 0.4 / 2 a step).
        constexpr float DeathCameraStep = 0.4F / 2.0F;
        const float moveStep = (shift ? DeathCameraStep * 5.0F : DeathCameraStep) * steps;
        const float rotStep = DegreesToRadians(shift ? 3.0F : 1.5F) * steps;
        float forward = _roamPadMoveY * DeathCameraStep * steps, right = _roamPadMoveX * DeathCameraStep * steps, rise = _roamPadRise * DeathCameraStep * steps;
        float stepH = DegreesToRadians(_roamPadLookX + mouseX / 1.5F) * steps;
        float stepV = DegreesToRadians(_roamPadLookY - mouseY / 1.5F) * steps;
        if (keys)
        {
            if (_keyboardState->IsKeyDown(Key::W)) forward += moveStep;
            else if (_keyboardState->IsKeyDown(Key::S)) forward -= moveStep;
            if (_keyboardState->IsKeyDown(Key::Space)) rise += moveStep;
            else if (_keyboardState->IsKeyDown(Key::V)) rise -= moveStep;
            if (_keyboardState->IsKeyDown(Key::A)) right -= moveStep;
            else if (_keyboardState->IsKeyDown(Key::D)) right += moveStep;
            if (_keyboardState->IsKeyDown(Key::Left)) stepH -= rotStep;
            else if (_keyboardState->IsKeyDown(Key::Right)) stepH += rotStep;
            if (_keyboardState->IsKeyDown(Key::Up)) stepV += rotStep;
            else if (_keyboardState->IsKeyDown(Key::Down)) stepV -= rotStep;
        }
        _cameraPosition = _cameraPosition + Multiply(_cameraFacing, forward) + Multiply(_cameraRight, right);
        _cameraPosition.Y += rise;
        if (stepH != 0.0F || stepV != 0.0F) UpdateCameraRotation(stepH, stepV);
    }

    void Scene::UpdatePointModule()
    {
        if (!Entities::PointModuleEntity::Current())
        {
            std::shared_ptr<Entities::EntityBase> entity;
            if (TryGetEntity(Entities::PointModuleEntity::StartId, entity))
            {
                if (auto module = std::dynamic_pointer_cast<Entities::PointModuleEntity>(entity))
                {
                    module->SetCurrent();
                }
            }
        }
        else
        {
            auto current = Entities::PointModuleEntity::Current();
            auto next = current->Next() ? current->Next() : current->Prev();
            if (next && next != current)
            {
                next->SetCurrent();
            }
            else
            {
                std::shared_ptr<Entities::EntityBase> entity;
                if (TryGetEntity(Entities::PointModuleEntity::StartId, entity))
                {
                    if (auto module = std::dynamic_pointer_cast<Entities::PointModuleEntity>(entity))
                    {
                        module->SetCurrent();
                    }
                }
            }
        }
    }

    void Scene::OnKeyDown(const RendererPlatform::KeyboardKeyEventArgs& e)
    {
        // The interactive console worker snapshots these same debug/output
        // fields. Managed C# tolerates concurrent reads; native std::string,
        // vectors and ordinary scalar fields do not. Use the same scene gate
        // that already serializes simulation, draw and room transitions.
        const std::lock_guard<std::recursive_mutex> gate(NativeRuntime::SceneGate());
#if defined(DEBUG)
        using RendererPlatform::Key;
        if (Selection::OnKeyDown(e, *this))
        {
            return;
        }
        if (e.Key == Key::R)
        {
            if (e.Control && e.Shift)
            {
                if (_recording)
                {
                    Images::StopRecording();
                }
                _recording = !_recording;
                _framesRecorded = 0;
            }
            else if (AllowCameraMovement() && _inputMode != InputMode::PlayerOnly)
            {
                ResetCamera();
            }
        }
        if (e.Key == Key::P)
        {
            if (e.Alt)
            {
                UpdatePointModule();
            }
            else if (e.Shift)
            {
                if (_cameraMode != MphRead::CameraMode::Player)
                {
                    if (_inputMode == InputMode::All) _inputMode = InputMode::PlayerOnly;
                    else if (_inputMode == InputMode::PlayerOnly) _inputMode = InputMode::CameraOnly;
                    else _inputMode = InputMode::All;
                }
            }
            else
            {
                if (_cameraMode == MphRead::CameraMode::Pivot)
                {
                    _cameraMode = MphRead::CameraMode::Roam;
                    _inputMode = InputMode::CameraOnly;
                }
                else if (_cameraMode == MphRead::CameraMode::Roam)
                {
                    _cameraMode = MphRead::CameraMode::Player;
                    _inputMode = InputMode::All;
                }
                else
                {
                    _cameraMode = MphRead::CameraMode::Pivot;
                    _inputMode = InputMode::CameraOnly;
                }
                ResetCamera();
            }
        }
        else if (e.Key == Key::Enter)
        {
            _frameAdvanceOn = !_frameAdvanceOn;
        }
        else if (e.Key == RendererPlatform::PeriodKey)
        {
            if (_frameAdvanceOn)
            {
                _advanceOneFrame = true;
            }
        }
        if (_inputMode == InputMode::PlayerOnly)
        {
            return;
        }
        if (e.Key == Key::J && _showCollision)
        {
            if (_colMenuSelect == 0)
            {
                if (e.Control) _colEntDisplay = EntityType::All;
                else if (e.Shift)
                {
                    if (_colEntDisplay == EntityType::Room) _colEntDisplay = EntityType::All;
                    else if (_colEntDisplay == EntityType::Object) _colEntDisplay = EntityType::Platform;
                    else if (_colEntDisplay == EntityType::All) _colEntDisplay = EntityType::Object;
                    else _colEntDisplay = EntityType::Room;
                }
                else
                {
                    if (_colEntDisplay == EntityType::Room) _colEntDisplay = EntityType::Platform;
                    else if (_colEntDisplay == EntityType::Platform) _colEntDisplay = EntityType::Object;
                    else if (_colEntDisplay == EntityType::Object) _colEntDisplay = EntityType::All;
                    else _colEntDisplay = EntityType::Room;
                }
            }
            else if (_colMenuSelect == 1)
            {
                if (e.Control) _colTerDisplay = Terrain::All;
                else if (e.Shift)
                {
                    _colTerDisplay = static_cast<Terrain>(static_cast<std::uint8_t>(_colTerDisplay) - 1U);
                    if (static_cast<std::uint8_t>(_colTerDisplay) == 255U) _colTerDisplay = Terrain::All;
                }
                else
                {
                    _colTerDisplay = static_cast<Terrain>(static_cast<std::uint8_t>(_colTerDisplay) + 1U);
                    if (_colTerDisplay > Terrain::All) _colTerDisplay = Terrain::Metal;
                }
            }
            else if (_colMenuSelect == 2)
            {
                if (e.Control) _colTypeDisplay = CollisionType::Any;
                else if (e.Shift)
                {
                    _colTypeDisplay = static_cast<CollisionType>(static_cast<std::int32_t>(_colTypeDisplay) - 1);
                    if (static_cast<std::int32_t>(_colTypeDisplay) < 0) _colTypeDisplay = CollisionType::Both;
                }
                else
                {
                    _colTypeDisplay = static_cast<CollisionType>(static_cast<std::int32_t>(_colTypeDisplay) + 1);
                    if (_colTypeDisplay > CollisionType::Both) _colTypeDisplay = CollisionType::Any;
                }
            }
            else if (_colMenuSelect == 3)
            {
                if (e.Control) _colDisplayColor = CollisionColor::None;
                else if (e.Shift)
                {
                    _colDisplayColor = static_cast<CollisionColor>(static_cast<std::int32_t>(_colDisplayColor) - 1);
                    if (static_cast<std::int32_t>(_colDisplayColor) < 0) _colDisplayColor = CollisionColor::Type;
                }
                else
                {
                    _colDisplayColor = static_cast<CollisionColor>(static_cast<std::int32_t>(_colDisplayColor) + 1);
                    if (_colDisplayColor > CollisionColor::Type) _colDisplayColor = CollisionColor::None;
                }
            }
            else if (_colMenuSelect == 4)
            {
                _colDisplayAlpha = _colDisplayAlpha == 1.0F ? 0.5F : 1.0F;
            }
        }
        else if (e.Key == Key::J && _showBotAiSlot != -1)
        {
            if (e.Shift)
            {
                if (_showBotAiSlot <= 0) _showBotAiSlot = 3;
                else --_showBotAiSlot;
            }
            else if (_showBotAiSlot >= 3) _showBotAiSlot = 0;
            else ++_showBotAiSlot;
        }
        else if (e.Key == Key::K)
        {
            if (e.Alt) _showCollision = !_showCollision;
            else if (_showCollision)
            {
                if (e.Control)
                {
                    _colMenuSelect = 0;
                    _colEntDisplay = EntityType::Room;
                    _colTerDisplay = Terrain::All;
                    _colTypeDisplay = CollisionType::Any;
                    _colDisplayColor = CollisionColor::None;
                    _colDisplayAlpha = 0.5F;
                }
                else if (e.Shift)
                {
                    --_colMenuSelect;
                    if (_colMenuSelect < 0) _colMenuSelect = 4;
                }
                else
                {
                    ++_colMenuSelect;
                    if (_colMenuSelect > 4) _colMenuSelect = 0;
                }
            }
        }
        else if (e.Key == Key::D5 && e.Shift)
        {
            if (!_recording) Images::Screenshot(Commands(), _rendererSize.X, _rendererSize.Y);
        }
        else if (e.Key == Key::T) _showTextures = !_showTextures;
        else if (e.Key == Key::C)
        {
            if (e.Alt)
            {
                if (e.Shift) _promptState = PromptState::CameraPos;
                else _outputCameraPos = !_outputCameraPos;
            }
            else if (e.Control) _showColors = !_showColors;
        }
        else if (e.Key == Key::Q)
        {
            if (e.Alt)
            {
                if (e.Shift)
                {
                    --_volumeEdges;
                    if (_volumeEdges < 0) _volumeEdges = 2;
                }
                else
                {
                    ++_volumeEdges;
                    if (_volumeEdges > 2) _volumeEdges = 0;
                }
            }
            else if (e.Control) _wireframeLevel = (_wireframeLevel + 1) % (MaxWireframeLevel + 1);
        }
        else if (e.Key == Key::B)
        {
            if (e.Alt) _showBotAiSlot = _showBotAiSlot == -1 ? 1 : -1;
            else
            {
                _faceCulling = !_faceCulling;
            }
        }
        else if (e.Key == Key::F) FilteringOn(!FilteringOn());
        else if (e.Key == Key::L) LightingOn(!LightingOn());
        else if (e.Key == Key::Z)
        {
            if (e.Control) _showVolumes = VolumeDisplay::None;
            else if (e.Shift)
            {
                _showVolumes = static_cast<VolumeDisplay>(static_cast<std::int32_t>(_showVolumes) - 1);
                if (_showVolumes < VolumeDisplay::None) _showVolumes = VolumeDisplay::Portal;
            }
            else
            {
                _showVolumes = static_cast<VolumeDisplay>(static_cast<std::int32_t>(_showVolumes) + 1);
                if (_showVolumes > VolumeDisplay::Portal) _showVolumes = VolumeDisplay::None;
            }
        }
        else if (e.Key == Key::G)
        {
            if (e.Alt) _useClip = !_useClip;
            else FogOn(!FogOn());
        }
        else if (e.Key == Key::N)
        {
            if (e.Alt) _showAllNodes = !_showAllNodes;
            else _transformRoomNodes = !_transformRoomNodes;
        }
        else if (e.Key == Key::H)
        {
            if (e.Alt) Selection::ToggleUnselectedVolumes();
            else Selection::ToggleShowSelection();
        }
        else if (e.Key == Key::I)
        {
            if (e.Alt) _showInvisible = _showInvisible == 2 ? 0 : 2;
            else _showInvisible = _showInvisible == 0 ? 1 : 0;
        }
        else if (e.Key == Key::Y) _showNodeData = !_showNodeData;
        else if (e.Key == Key::E && e.Shift && !e.Alt) _scanVisor = !_scanVisor;
        else if (e.Control && e.Key == Key::O) _promptState = PromptState::Load;
        else if (e.Control && e.Key == Key::U)
        {
            if (Selection::Entity()) _unloadQueue.Enqueue(Selection::Entity());
        }
#else
        (void)e;
#endif
    }

    void Scene::OutputStart()
    {
        _outputThread = RendererJThread([this](RendererStopToken token)
        {
            OutputUpdate(token);
        });
    }

    void Scene::OutputStop()
    {
        if (_outputThread.joinable())
        {
            _outputThread.request_stop();
        }
    }

    void Scene::OutputUpdate(RendererStopToken token)
    {
        std::mutex delayMutex;
        std::condition_variable_any delayCondition;
        while (!token.stop_requested())
        {
            PromptState prompt = PromptState::None;
            {
                const std::lock_guard<std::recursive_mutex> gate(NativeRuntime::SceneGate());
                prompt = _promptState;
                _promptState = PromptState::None;
            }
            if (prompt == PromptState::Load)
            {
                OutputLoadPrompt();
                _currentOutput.clear();
            }
            else if (prompt == PromptState::CameraPos)
            {
                OutputCameraPrompt();
                _currentOutput.clear();
            }

            std::string output;
            {
                // OutputGetAll walks live scene/entity/selection state. In C#
                // those reads cannot corrupt managed containers. In native C++
                // racing a frame update is undefined behaviour, so take the
                // same gate as the writers while producing the snapshot.
                const std::lock_guard<std::recursive_mutex> gate(NativeRuntime::SceneGate());
                output = OutputGetAll();
            }
            if (output != _currentOutput)
            {
                NativeRuntime::ConsoleClear();
                ::MphRead::NativeRuntime::ConsoleWriteLine(output);
                _currentOutput = output;
            }
            std::unique_lock lock(delayMutex);
            RendererWaitForStop(delayCondition, lock, token, std::chrono::milliseconds(100));
        }
    }

    void Scene::OutputLoadPrompt()
    {
        NativeRuntime::ConsoleClear();
        ::MphRead::NativeRuntime::ConsoleWrite("Enter model name: ");
        std::string line = NativeRuntime::ConsoleReadLine().value_or(std::string{});
        auto trim = [](std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return std::string{};
            }
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        };
        line = trim(std::move(line));
        std::vector<std::string> input;
        std::size_t start = 0;
        while (start <= line.size())
        {
            const std::size_t end = line.find(' ', start);
            input.emplace_back(line.substr(start,
                end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        const std::string name = input.empty() ? std::string{} : trim(input[0]);
        if (!name.empty())
        {
            std::int32_t recolor = 0;
            bool firstHunt = false;
            if (input.size() > 1)
            {
                const std::string valueText = trim(input[1]);
                std::uint32_t value = 0;
                const auto result = std::from_chars(valueText.data(),
                    valueText.data() + valueText.size(), value);
                if (result.ec == std::errc{} && result.ptr == valueText.data() + valueText.size())
                {
                    recolor = std::bit_cast<std::int32_t>(value);
                }
                if (input.size() > 2)
                {
                    firstHunt = trim(input[2]) == "-fh";
                }
            }
            _loadQueue.Enqueue(std::make_tuple(name, recolor, firstHunt));
        }
    }

    void Scene::OutputCameraPrompt()
    {
        NativeRuntime::ConsoleClear();
        ::MphRead::NativeRuntime::ConsoleWrite("Enter camera position: ");
        std::string line = NativeRuntime::StringTrim(
            NativeRuntime::ConsoleReadLine().value_or(std::string{}));
        line.erase(std::remove(line.begin(), line.end(), ','), line.end());
        std::vector<std::string> input;
        std::size_t start = 0;
        while (start <= line.size())
        {
            const std::size_t end = line.find(' ', start);
            input.emplace_back(line.substr(start,
                end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        float coords[3]{0.0F, 0.0F, 0.0F};
        for (std::int32_t i = 0; i < 3 && static_cast<std::size_t>(i) < input.size(); ++i)
        {
            const std::string& item = input[static_cast<std::size_t>(i)];
            float coord = 0.0F;
            if (item.rfind("0x", 0) == 0)
            {
                std::string hex = item;
                for (std::size_t pos = 0; (pos = hex.find("0x", pos)) != std::string::npos;)
                {
                    hex.erase(pos, 2);
                }
                std::int32_t parsed = 0;
                if (NativeRuntime::Int32TryParseHexNumber(std::move(hex), parsed))
                {
                    coord = static_cast<float>(parsed) / 4096.0F;
                }
            }
            else
            {
                (void)NativeRuntime::SingleTryParseCurrentCulture(item, coord);
            }
            coords[i] = coord;
        }
        {
            const std::lock_guard<std::recursive_mutex> gate(NativeRuntime::SceneGate());
            _cameraPosition = Vector3(coords[0], coords[1], coords[2]);
        }
    }

    std::string Scene::OutputGetAll()
    {
        std::ostringstream out;
        out << Mods::Branding::Name << ' ' << Program::Version.ToString()
            << (_recording ? " - Recording" : "")
            << (_frameAdvanceOn ? " - Frame Advance" : "") << '\n';
        _outputBuffer = out.str();
        if (_showBotAiSlot >= 0 && _showBotAiSlot <= 3)
        {
            OutputGetBotAi();
        }
        else
        {
            if (_showCollision)
            {
                OutputGetCollisionMenu();
            }
            if (Selection::Entity())
            {
                OutputGetEntityInfo();
                if (Selection::Instance())
                {
                    OutputGetModel();
                    if (Selection::Node())
                    {
                        OutputGetNode();
                        if (Selection::Mesh())
                        {
                            OutputGetMesh();
                        }
                    }
                }
            }
            else if (!_showCollision)
            {
                OutputGetMenu();
            }
        }
        return _outputBuffer;
    }

    void Scene::OutputGetBotAi()
    {
        std::ostringstream out;
        out << '\n' << "Bot AI slot: " << _showBotAiSlot
            << " (J: Next slot, Shift+J: Previous slot)\n";
        const auto player = Entities::PlayerEntity::Players().at(
            static_cast<std::size_t>(_showBotAiSlot));
        if (!((player->LoadFlags() & LoadFlags::SlotActive) == LoadFlags::SlotActive)
            && !((player->LoadFlags() & LoadFlags::Active) == LoadFlags::Active))
        {
            out << "none\n";
        }
        else if (!player->IsBot())
        {
            out << "Player - " << EnumText(player->Hunter()) << '\n';
        }
        else
        {
            out << "Bot - " << EnumText(player->Hunter()) << '\n';
            _outputBuffer += out.str();
            player->AiData->GetOuptut(_outputBuffer);
            return;
        }
        _outputBuffer += out.str();
    }

    void Scene::OutputGetCollisionMenu()
    {
        std::ostringstream out;
        out << '\n'
            << '[' << (_colMenuSelect == 0 ? 'x' : ' ') << "] Entities ("
            << EnumText(_colEntDisplay) << ")\n"
            << '[' << (_colMenuSelect == 1 ? 'x' : ' ') << "] Terrain ("
            << EnumText(_colTerDisplay) << ")\n"
            << '[' << (_colMenuSelect == 2 ? 'x' : ' ') << "] Interaction ("
            << EnumText(_colTypeDisplay) << ")\n"
            << '[' << (_colMenuSelect == 3 ? 'x' : ' ') << "] Color mode ("
            << EnumText(_colDisplayColor) << ")\n"
            << '[' << (_colMenuSelect == 4 ? 'x' : ' ') << "] Opacity ("
            << _colDisplayAlpha << ")\n\n"
            << "K: Next option, Shift+K: Previous option, Alt+K: Hide collision\n"
            << "J: Next value, Shift+J: Previous value, Ctrl+J: Reset value, Ctrl+K: Reset all\n";
        _outputBuffer += out.str();
    }

    void Scene::OutputGetMenu()
    {
        std::ostringstream out;
        out << '\n';
        if (_cameraMode == MphRead::CameraMode::Pivot)
        {
            out << " - Scroll mouse wheel to zoom\n";
        }
        else if (_cameraMode == MphRead::CameraMode::Roam)
        {
            out << " - Use WASD, Space, and V to move\n";
        }
        std::string volume = "off";
        switch (_showVolumes)
        {
        case VolumeDisplay::LightColor1: volume = "light sources, color 1"; break;
        case VolumeDisplay::LightColor2: volume = "light sources, color 2"; break;
        case VolumeDisplay::TriggerParent: volume = "trigger volumes, parent event"; break;
        case VolumeDisplay::TriggerChild: volume = "trigger volumes, child event"; break;
        case VolumeDisplay::AreaInside: volume = "area volumes, inside event"; break;
        case VolumeDisplay::AreaExit: volume = "area volumes, exit event"; break;
        case VolumeDisplay::MorphCamera: volume = "morph cameras"; break;
        case VolumeDisplay::JumpPad: volume = "jump pads"; break;
        case VolumeDisplay::Teleporter: volume = "teleporters"; break;
        case VolumeDisplay::EnemyHurt: volume = "enemy hurtboxes"; break;
        case VolumeDisplay::Object: volume = "objects"; break;
        case VolumeDisplay::FlagBase: volume = "flag bases"; break;
        case VolumeDisplay::DefenseNode: volume = "defense nodes"; break;
        case VolumeDisplay::KillPlane: volume = "kill plane"; break;
        case VolumeDisplay::PlayerLimit: volume = "room limits (player)"; break;
        case VolumeDisplay::CameraLimit: volume = "room limits (camera)"; break;
        case VolumeDisplay::NodeBounds: volume = "room node bounds"; break;
        case VolumeDisplay::NodeData: volume = "node data radius"; break;
        case VolumeDisplay::Portal: volume = "portals"; break;
        default: break;
        }
        const std::string invisible = _showInvisible == 2 ? "all"
            : _showInvisible == 1 ? "placeholders" : "off";
        const std::string input = _inputMode == InputMode::PlayerOnly ? "player only"
            : _inputMode == InputMode::CameraOnly ? "camera only" : "all";
        out << " - Hold left mouse button or use arrow keys to rotate\n"
            << " - Hold Shift to move the camera faster\n"
            << " - T toggles texturing (" << OnOff(_showTextures) << ")\n"
            << " - Ctrl+C toggles vertex colors (" << OnOff(_showColors) << ")\n"
            << " - Ctrl+Q cycles wireframe (level " << _wireframeLevel << "/" << MaxWireframeLevel << ")\n"
            << " - B toggles face culling (" << OnOff(_faceCulling) << ")\n"
            << " - F toggles texture filtering (" << OnOff(FilteringOn()) << ")\n"
            << " - L toggles lighting (" << OnOff(LightingOn()) << ")\n"
            << " - G toggles fog (" << OnOff(FogOn()) << ")\n"
            << " - Shift+E toggles Scan Visor (" << OnOff(_scanVisor) << ")\n"
            << " - I toggles invisible entities (" << invisible << ")\n"
            << " - Z toggles volume display (" << volume << ")\n"
            << " - P switches camera mode ("
            << (_cameraMode == MphRead::CameraMode::Pivot ? "pivot" : "roam") << ")\n"
            << " - Shift+P switches input mode (" << input << ")\n"
            << " - R resets the camera\n"
            << " - Ctrl+O then enter \"model_name [recolor]\" to load\n"
            << " - Ctrl+U then enter \"model_id\" to unload\n"
            << " - Esc closes the viewer\n";
        _outputBuffer += out.str();
    }

    void Scene::OutputGetEntityInfo()
    {
        const auto entity = Selection::Entity();
        MPHREAD_DEBUG_ASSERT(entity);
        std::ostringstream out;
        out << '\n';
        if (_roomLoaded)
        {
            const auto toColor = [](Vector3 color)
            {
                return std::to_string(static_cast<std::int32_t>(color.X * 255.0F)) + ";"
                    + std::to_string(static_cast<std::int32_t>(color.Y * 255.0F)) + ";"
                    + std::to_string(static_cast<std::int32_t>(color.Z * 255.0F));
            };
            out << "Room \x1b[38;2;" << toColor(_light1Color) << "m████\x1b[0m "
                << "\x1b[38;2;" << toColor(_light2Color) << "m████\x1b[0m"
                << " (" << _light1Vector.X << ", " << _light1Vector.Y << ", " << _light1Vector.Z << ")"
                << " (" << _light2Vector.X << ", " << _light2Vector.Y << ", " << _light2Vector.Z << ")\n";
        }
        else
        {
            out << "No room loaded\n";
        }

        if (_outputCameraPos)
        {
            out << "Camera (" << _cameraPosition.X << ", " << _cameraPosition.Y
                << ", " << _cameraPosition.Z << ")\n";
        }
        else
        {
            out << "Camera (?, ?, ?)\n";
        }

        out << "\nEntity: " << EnumText(entity->Type);
        const auto& models = entity->GetModels();
        if (entity->Type == EntityType::Model)
        {
            MPHREAD_DEBUG_ASSERT(!models.empty());
            out << " (" << models[0]->Model()->Name << ")";
        }
        std::string color;
        if (!models.empty() && !models[0]->IsPlaceholder)
        {
            color = " - Color " + std::to_string(entity->Recolor());
        }
        out << " [" << entity->Id << "] " << (entity->Active ? "On " : "Off") << color;

        if (entity->Type == EntityType::Room)
        {
            const auto& nodes = *entity->GetModels()[0]->Model()->Nodes;
            const auto count = std::count_if(nodes.begin(), nodes.end(),
                [](const std::shared_ptr<Node>& node) { return node->RoomPartId >= 0; });
            out << " (" << count << ")";
        }
        else if (const auto light = std::dynamic_pointer_cast<Entities::LightSourceEntity>(entity))
        {
            const auto toColor = [](Vector3 value)
            {
                return std::to_string(static_cast<std::int32_t>(value.X * 255.0F)) + ";"
                    + std::to_string(static_cast<std::int32_t>(value.Y * 255.0F)) + ";"
                    + std::to_string(static_cast<std::int32_t>(value.Z * 255.0F));
            };
            const Vector3 color1 = light->Light1Color();
            const Vector3 color2 = light->Light2Color();
            out << " \x1b[38;2;" << toColor(color1) << "m████\x1b[0m"
                << " \x1b[38;2;" << toColor(color2) << "m████\x1b[0m"
                << " " << BoolText(light->Light1Enabled()) << " / " << BoolText(light->Light2Enabled());
            const Vector3 vector1 = light->Light1Vector();
            const Vector3 vector2 = light->Light2Vector();
            out << " (" << vector1.X << ", " << vector1.Y << ", " << vector1.Z << ")"
                << " (" << vector2.X << ", " << vector2.Y << ", " << vector2.Z << ")";
        }
        else if (const auto area = std::dynamic_pointer_cast<Entities::AreaVolumeEntity>(entity))
        {
            const auto data = area->Data();
            const auto* parent = area->GetParent();
            const auto* child = area->GetChild();
            out << " (" << EnumText(data.TriggerFlags) << ")\n";
            out << "Entry: " << EnumText(data.InsideMessage)
                << ", Param1: " << data.InsideMsgParam1 << ", Param2: " << data.InsideMsgParam2
                << ", Target: " << (parent ? EnumText(parent->Type) : "None")
                << " (" << data.ParentId << ")\n";
            out << " Exit: " << EnumText(data.ExitMessage)
                << ", Param1: " << data.ExitMsgParam1 << ", Param2: " << data.ExitMsgParam2
                << ", Target: " << (child ? EnumText(child->Type) : "None")
                << " (" << data.ChildId << ")";
        }
        else if (const auto area = std::dynamic_pointer_cast<Entities::FhAreaVolumeEntity>(entity))
        {
            const auto data = area->Data();
            out << " (" << EnumText(data.TriggerFlags) << ")\n";
            out << "Entry: " << EnumText(data.InsideMessage)
                << ", Param1: " << data.InsideMsgParam1 << ", Param2: 0\n";
            out << " Exit: " << EnumText(data.ExitMessage)
                << ", Param1: " << data.ExitMsgParam1 << ", Param2: 0";
        }
        else if (const auto trigger = std::dynamic_pointer_cast<Entities::TriggerVolumeEntity>(entity))
        {
            const auto data = trigger->Data();
            const auto* parent = trigger->GetParent();
            const auto* child = trigger->GetChild();
            out << " (" << EnumText(data.Subtype);
            if (data.Subtype == TriggerType::Threshold)
            {
                out << " x" << data.TriggerThreshold;
            }
            out << ") (" << EnumText(data.TriggerFlags) << ")\n";
            out << "Parent: " << EnumText(data.ParentMessage)
                << ", Param1: " << data.ParentMsgParam1 << ", Param2: " << data.ParentMsgParam2
                << ", Target: " << (parent ? EnumText(parent->Type) : "None")
                << " (" << data.ParentId << ")\n";
            out << " Child: " << EnumText(data.ChildMessage)
                << ", Param1: " << data.ChildMsgParam1 << ", Param2: " << data.ChildMsgParam2
                << ", Target: " << (child ? EnumText(child->Type) : "None")
                << " (" << data.ChildId << ")";
        }
        else if (const auto trigger = std::dynamic_pointer_cast<Entities::FhTriggerVolumeEntity>(entity))
        {
            const auto data = trigger->Data();
            if (data.Subtype == FhTriggerType::Threshold)
            {
                out << " x" << data.Threshold;
            }
            out << " (" << EnumText(data.TriggerFlags) << ")\n";
            out << "Parent: " << EnumText(data.ParentMessage)
                << ", Param1: " << data.ParentMsgParam1 << ", Param2: 0";
            std::shared_ptr<Entities::EntityBase> parent;
            if (data.ParentMessage != FhMessage::None && TryGetEntity(data.ParentId, parent))
            {
                out << ", Target: " << EnumText(parent->Type) << " (" << data.ParentId << ")";
            }
            else
            {
                out << ", Target: None";
            }
            out << '\n';
            out << " Child: " << EnumText(data.ChildMessage)
                << ", Param1: " << data.ChildMsgParam1 << ", Param2: 0";
            std::shared_ptr<Entities::EntityBase> child;
            if (data.ChildMessage != FhMessage::None && TryGetEntity(data.ChildId, child))
            {
                out << ", Target: " << EnumText(child->Type) << " (" << data.ChildId << ")";
            }
            else
            {
                out << ", Target: None";
            }
        }
        else if (const auto spawn = std::dynamic_pointer_cast<Entities::EnemySpawnEntity>(entity))
        {
            out << " (" << EnumText(spawn->Data.EnemyType) << ")";
        }
        else if (const auto enemy = std::dynamic_pointer_cast<Entities::EnemyInstanceEntity>(entity))
        {
            out << " (" << EnumText(enemy->EnemyType()) << ")";
        }
        else if (const auto item = std::dynamic_pointer_cast<Entities::ItemSpawnEntity>(entity))
        {
            out << " (" << EnumText(item->Data().ItemType) << ")";
        }
        else if (const auto object = std::dynamic_pointer_cast<Entities::ObjectEntity>(entity))
        {
            const auto data = object->Data();
            if (data.EffectId > 0)
            {
                out << " (" << data.EffectId << ", "
                    << Metadata::Effects.at(static_cast<std::size_t>(data.EffectId)).first << ")";
            }
        }
        else if (const auto cam = std::dynamic_pointer_cast<Entities::CamSeqEntity>(entity))
        {
            out << " (ID " << cam->Data().SequenceId << ")";
        }
        else if (const auto player = std::dynamic_pointer_cast<Entities::PlayerEntity>(entity))
        {
            out << " (Health: " << player->Health() << ")";
        }

        const Vector3 position = entity->Position;
        const Vector3 rotation = entity->Rotation;
        const Vector3 scale = entity->Scale;
        out << '\n'
            << "Position (" << position.X << ", " << position.Y << ", " << position.Z << ")\n"
            << "Rotation (" << rotation.X << ", " << rotation.Y << ", " << rotation.Z << ")\n"
            << "   Scale (" << scale.X << ", " << scale.Y << ", " << scale.Z << ")\n";
        _outputBuffer += out.str();
    }

    void Scene::OutputGetModel()
    {
        const auto inst = Selection::Instance();
        MPHREAD_DEBUG_ASSERT(inst);
        const auto model = inst->Model();
        std::ostringstream out;
        out << '\n'
            << "Model: " << model->Name << ", Scale: " << model->Scale.X
            << ", Active: " << YesNo(inst->Active)
            << (inst->IsPlaceholder ? ", Placeholder" : "") << '\n';
        out << "Nodes " << model->Nodes->size()
            << ", Meshes " << model->Meshes->size()
            << ", Materials " << model->Materials->size()
            << ", Textures " << model->Recolors->at(0)->Textures->size()
            << ", Palettes " << model->Recolors->at(0)->Palettes->size() << '\n';

        const auto& a = *inst->AnimInfo;
        const auto& g = *model->AnimationGroups;
        const auto hasGroup = [](const auto& info)
        {
            return info->Group && info->Group->Count > 0;
        };
        out << "Anim: " << (*a.Index)[0] << ", " << (*a.Frame)[1]
            << " (Node " << (hasGroup(a.Node) ? a.NodeIndex() : -1) << " / " << g.Node->size()
            << ", Mat " << (hasGroup(a.Material) ? a.MaterialIndex() : -1) << " / " << g.Material->size()
            << ", UV " << (hasGroup(a.Texcoord) ? a.TexcoordIndex() : -1) << " / " << g.Texcoord->size()
            << ", Tex " << (hasGroup(a.Texture) ? a.TextureIndex() : -1) << " / " << g.Texture->size()
            << ")\n";
        _outputBuffer += out.str();
    }

    void Scene::OutputGetNode()
    {
        const auto node = Selection::Node();
        const auto inst = Selection::Instance();
        MPHREAD_DEBUG_ASSERT(node && inst);
        const auto model = inst->Model();

        const auto formatNode = [&model](std::int32_t index)
        {
            if (index == -1)
            {
                return std::string("None");
            }
            return model->Nodes->at(static_cast<std::size_t>(index))->Name
                + " [" + std::to_string(index) + "]";
        };

        std::vector<std::int32_t> meshIds;
        for (std::int32_t id : node->GetMeshIds())
        {
            meshIds.push_back(id);
        }
        std::sort(meshIds.begin(), meshIds.end());

        std::string mesh = " - Meshes " + std::to_string(node->MeshCount);
        if (meshIds.size() == 1)
        {
            mesh += " (" + std::to_string(meshIds.front()) + ")";
        }
        else if (meshIds.size() > 1)
        {
            mesh += " (" + std::to_string(meshIds.front()) + " - "
                + std::to_string(meshIds.back()) + ")";
        }

        const auto it = std::find(model->Nodes->begin(), model->Nodes->end(), node);
        const std::int32_t index = it == model->Nodes->end()
            ? -1
            : static_cast<std::int32_t>(std::distance(model->Nodes->begin(), it));
        const std::string enabled = node->Enabled
            ? (model->NodeParentsEnabled(node) ? "On " : "On*")
            : "Off";
        const std::string billboard = node->BillboardMode != BillboardMode::None
            ? " - " + EnumText(node->BillboardMode) + " Billboard"
            : "";

        std::ostringstream out;
        out << '\n'
            << "Node: " << node->Name << " [" << index << "] "
            << enabled << mesh << billboard << '\n'
            << "Parent " << formatNode(node->ParentIndex) << '\n'
            << " Child " << formatNode(node->ChildIndex) << '\n'
            << "  Next " << formatNode(node->NextIndex) << '\n'
            << "Position (" << node->Position.X << ", " << node->Position.Y << ", " << node->Position.Z << ")\n"
            << "Rotation (" << node->Angle.X << ", " << node->Angle.Y << ", " << node->Angle.Z << ")\n"
            << "   Scale (" << node->Scale.X << ", " << node->Scale.Y << ", " << node->Scale.Z << ")\n";
        _outputBuffer += out.str();
    }

    void Scene::OutputGetMesh()
    {
        const auto mesh = Selection::Mesh();
        const auto inst = Selection::Instance();
        MPHREAD_DEBUG_ASSERT(mesh && inst);
        const auto model = inst->Model();
        const auto it = std::find(model->Meshes->begin(), model->Meshes->end(), mesh);
        const std::int32_t index = it == model->Meshes->end()
            ? -1
            : static_cast<std::int32_t>(std::distance(model->Meshes->begin(), it));
        const auto& material = *model->Materials->at(static_cast<std::size_t>(mesh->MaterialId));

        std::ostringstream out;
        out << '\n'
            << "Mesh: [" << index << "] " << (mesh->Visible ? "On " : "Off")
            << " - Material ID " << mesh->MaterialId << ", DList ID " << mesh->DlistId << "\n\n"
            << "Material: " << material.Name << " [" << mesh->MaterialId << "] - "
            << EnumText(material.RenderMode) << ", " << EnumText(material.PolygonMode)
            << " - " << EnumText(material.TexgenMode) << '\n'
            << "Lighting " << static_cast<std::int32_t>(material.Lighting)
            << ", Alpha " << static_cast<std::int32_t>(material.Alpha)
            << ", XRepeat " << EnumText(material.XRepeat)
            << ", YRepeat " << EnumText(material.YRepeat) << '\n'
            << "Texture ID " << material.CurrentTextureId
            << ", Palette ID " << material.CurrentPaletteId << '\n'
            << "Diffuse (" << static_cast<std::int32_t>(material.Diffuse.Red) << ", "
            << static_cast<std::int32_t>(material.Diffuse.Green) << ", "
            << static_cast<std::int32_t>(material.Diffuse.Blue) << ")"
            << " Ambient (" << static_cast<std::int32_t>(material.Ambient.Red) << ", "
            << static_cast<std::int32_t>(material.Ambient.Green) << ", "
            << static_cast<std::int32_t>(material.Ambient.Blue) << ")"
            << " Specular (" << static_cast<std::int32_t>(material.Specular.Red) << ", "
            << static_cast<std::int32_t>(material.Specular.Green) << ", "
            << static_cast<std::int32_t>(material.Specular.Blue) << ")\n";
        _outputBuffer += out.str();
    }

    std::string Scene::OnOff(bool setting)
    {
        return setting ? "on" : "off";
    }

    std::string Scene::YesNo(bool setting)
    {
        return setting ? "yes" : "no ";
    }

#if !defined(__ANDROID__)
    const RendererPlatform::WindowSettings& RenderWindow::Settings()
    {
        // A Vulkan scene presents the window itself, so the window has no GL
        // context at all; an OpenGL one is its context.
        // Asked again for every window: Settings can switch the renderer and
        // remake the window on the other one.
        static RendererPlatform::WindowSettings settings{};
        settings = Mods::Render::DesktopGlContext::Settings(
            false, NativeRuntime::Rhi::ScenePresentsWindow()
                ? RendererPlatform::GraphicsWindowMode::NoApi : RendererPlatform::GraphicsWindowMode::OpenGL);
        return settings;
    }

    std::function<void(std::int32_t, std::string)> RenderWindow::_glfwErrorCallback{};

    void RenderWindow::LogCreatingWindow()
    {
        // Accessing any static member of the C# type runs its static field
        // initializers before the member body. Settings() is the native owner
        // of those one-time settings values, so force that ordering here too.
        (void)Settings();
        Mods::DebugLog::Line("render", "creating the game window and GL context ("
            + WindowStartModeName(Mods::Launcher::LauncherPrefs::WindowMode()) + ")");
    }

    RenderWindow::RenderWindow(bool shell)
        : _shell(shell)
    {
        CreateWindowOrFallBack();
        _performance = Mods::Diagnostics::FramePerformance::Create();
        const Vector2i clientSize = _window->ClientSize();
        const Vector2i size = _window->Size();
        Mods::DebugLog::Line("render", "game window created, " + std::to_string(clientSize.X)
            + "x" + std::to_string(clientSize.Y) + " client, " + std::to_string(size.X)
            + "x" + std::to_string(size.Y) + " pixels");
        if (_shell)
        {
            Mods::WindowGeometry::Owned(true);
            Mods::WindowGeometry::Restore(*this, _minimumSize);
        }
        if (!_shell)
        {
            _scene = NewScene();
        }
        _sceneReady = true;
        FitToScreen();
    }

    // Auto is Vulkan first, as on Android: a machine whose driver passes the
    // probe but cannot make the device or the swapchain plays on OpenGL
    // rather than stopping on an error. Nothing lives on the Vulkan session
    // yet, so the window is simply made again on the other renderer. An
    // explicit Vulkan still fails with its error.
    void RenderWindow::CreateWindowOrFallBack()
    {
        namespace Rhi = NativeRuntime::Rhi;
        const bool automatic = Rhi::RequestedSceneBackend() == Rhi::SceneBackendRequest::Auto;
        try
        {
            _window = RendererPlatform::CreateWindow(Settings());
            CreatePresentation();
            return;
        }
        catch (const std::exception& ex)
        {
            if (!automatic || Rhi::SelectedSceneBackend() != Rhi::GraphicsBackend::Vulkan) throw;
            std::cout << "[render] auto: Vulkan could not start (" << ex.what() << "); falling back to OpenGL"
                      << std::endl;
            Mods::DebugLog::Line("render", std::string("auto: Vulkan could not start, OpenGL instead: ") + ex.what());
        }
        _swapchain.reset();
        Rhi::DetachSceneWindow();
        _window.reset();
        Rhi::SelectSceneBackend(Rhi::GraphicsBackend::OpenGl);
        Rhi::NoteAutoFallBack();
        _window = RendererPlatform::CreateWindow(Settings());
        CreatePresentation();
    }

    void RenderWindow::CreatePresentation()
    {
        NativeRuntime::Rhi::SwapchainDesc swapchainDesc{};
        const Vector2i framebufferSize = _window->Size();
        swapchainDesc.width = static_cast<std::uint32_t>(std::max(framebufferSize.X, 1));
        swapchainDesc.height = static_cast<std::uint32_t>(std::max(framebufferSize.Y, 1));
        _swapchain = NativeRuntime::Rhi::CreateSceneWindowSwapchain(*_window, swapchainDesc);
        IgnoreUnavailableGlfwFeatures();
#if !defined(__ANDROID__)
        if (const RendererPlatform::WindowIcon* icon = Mods::Render::AppIcon::Load())
        {
            _window->SetIcon(*icon);
        }
#endif
        const std::string backend = NativeRuntime::Rhi::DescribeSceneBackend(_swapchain.get());
        std::cout << "[render] backend " << backend << std::endl;
        Mods::DebugLog::Line("render", "backend " + backend);
    }

    void RenderWindow::RequestRendererSwitch(NativeRuntime::Rhi::SceneBackendRequest request)
    {
        if (request == NativeRuntime::Rhi::RequestedSceneBackend()) return;
        // Auto already fell back to OpenGL this run: asking for Auto again
        // (Settings saved with the renderer untouched) is what is running.
        if (request == NativeRuntime::Rhi::SceneBackendRequest::Auto && NativeRuntime::Rhi::AutoFellBack()) return;
        _rendererSwitch = request;
        _window->Close();
    }

    // The window and everything on its device go; the scene's simulation,
    // the launcher's screens and every handle survive, and are put back on
    // the new device. A backend that cannot start is said, and the one that
    // was running comes back.
    void RenderWindow::SwitchRenderer(NativeRuntime::Rhi::SceneBackendRequest request)
    {
        if (ObserveRendererSwitch) ObserveRendererSwitch(*this, true);
        const NativeRuntime::Rhi::SceneBackendRequest previous = NativeRuntime::Rhi::RequestedSceneBackend();
        const Vector2i clientSize = ClientSize();
        const Vector2i location = Location();
        const std::int32_t border = WindowBorder();
        const auto state = WindowState();
        Mods::DebugLog::Line("render", std::string("switching the renderer to ")
            + std::string(NativeRuntime::Rhi::SceneBackendRequestName(request)) + " in place");
        if (_shell) Mods::WindowGeometry::Remember(*this);
        const auto release = [&]
        {
            if (_performance) _performance->Reset();
            if (_scene) _scene->ReleaseGpuForSwitch();
            if (BeforeRendererSwitch) BeforeRendererSwitch();
#if defined(MPHREAD_SHELL)
            if (_shell) Mods::Render::LauncherHunter::ReleaseGl();
#endif
            _windowCommands.reset();
            _swapchain.reset();
            if (ObserveRendererSwitchStage) ObserveRendererSwitchStage(*this, RendererSwitchStage::ReleasedResources);
            NativeRuntime::Rhi::DetachSceneWindow();
            _window.reset();
        };
        const auto create = [&](NativeRuntime::Rhi::SceneBackendRequest target)
        {
            _appliedFrameRateCap = -2;
            // Replacement windows start hidden, including a recovery window.
            _startedHidden = true;
            _applyStartupIn = 0;
            NativeRuntime::Rhi::ReselectSceneBackend(target);
            if (ObserveRendererSwitchStage) ObserveRendererSwitchStage(*this, RendererSwitchStage::BeforeWindow);
            _window = RendererPlatform::CreateWindow(Settings());
            CreatePresentation();
            if (ObserveRendererSwitchStage) ObserveRendererSwitchStage(*this, RendererSwitchStage::Presentation);
            FitToScreen();
            WindowBorder(border);
            Location(location);
            ClientSize(clientSize);
            if (state == RendererPlatform::WindowStateValue::Maximized) WindowStateMaximized();
            if (state == RendererPlatform::WindowStateValue::Fullscreen) (void)WindowStateFullscreen();
            Floating(Mods::WindowMode::IsFullscreen());
            if (_scene)
            {
                _scene->RebindInput(_window->Keyboard(), _window->Mouse());
                _scene->Size(FramebufferSize());
                _scene->RebuildGpuAfterSwitch([&]
                {
                    if (ObserveRendererSwitchStage) ObserveRendererSwitchStage(*this, RendererSwitchStage::Resources);
                });
            }
            if (AfterRendererSwitch) AfterRendererSwitch(*this);
            if (!_scene && ObserveRendererSwitchStage) ObserveRendererSwitchStage(*this, RendererSwitchStage::Resources);
        };
        release();
        try { create(request); }
        catch (const std::exception&)
        {
            const auto incoming = std::current_exception();
            Mods::DebugLog::Exception("renderer switch", incoming);
            try { release(); create(previous); }
            catch (const std::exception&)
            {
                const auto recovery = std::current_exception();
                // Release partial recovery resources while their device and
                // context still exist. Preserve both failures if cleanup fails.
                try { release(); } catch (...) {}
                if (ReportRendererSwitchFailure) ReportRendererSwitchFailure(incoming, false);
                throw NativeRuntime::Rhi::SceneBackendRecoveryFailed(
                    "Renderer switch failed: " + NativeRuntime::ExceptionMessage(incoming)
                    + "\nRecovery also failed: " + NativeRuntime::ExceptionMessage(recovery), incoming, recovery);
            }
            if (ReportRendererSwitchFailure) ReportRendererSwitchFailure(incoming, true);
            else NativeRuntime::ShowErrorDialog(std::string(Mods::Branding::Name),
                NativeRuntime::ExceptionMessage(incoming) + "\n\nThe renderer you were using is back.");
        }
        if (ObserveRendererSwitch) ObserveRendererSwitch(*this, false);
    }

    RenderWindow::~RenderWindow()
    {
        // Teardown must reach the session/context even when a lost device
        // rejects idle. Explicit operations retain and report the first error.
        const auto cleanup = [](auto&& action) noexcept
        {
            try { action(); }
            catch (...) {}
        };
        cleanup([this] { _performance.reset(); });
        if (_shell && BeforeRendererSwitch) cleanup([] { BeforeRendererSwitch(); });
#if defined(MPHREAD_SHELL)
        if (_shell)
        {
            // LauncherHunter owns a process-static side Scene. Release its GL
            // resources and destroy that Scene before _window tears down GLFW
            // and the owning OpenGL context.
            cleanup([] { Mods::Render::LauncherHunter::ReleaseGl(); });
        }
#endif
        if (_scene)
        {
            cleanup([this] { _scene->ReleaseGpuResources(); });
        }
        _windowCommands.reset();
        _swapchain.reset();
        if (ObserveRendererSwitchStage)
            cleanup([this] { ObserveRendererSwitchStage(*this, RendererSwitchStage::FinalRelease); });
        NativeRuntime::Rhi::DetachSceneWindow();
    }

    bool RenderWindow::HasScene() const noexcept
    {
        return _scene != nullptr;
    }

    MphRead::Scene& RenderWindow::Scene() const
    {
        return *_scene;
    }

    Vector2i RenderWindow::FramebufferSize() const
    {
        return _window->Size();
    }

    void* RenderWindow::WindowPtr() const
    {
        return _window->NativeHandle();
    }

    void RenderWindow::Title(std::string value)
    {
        _window->Title(std::move(value));
    }

    std::shared_ptr<MphRead::Scene> RenderWindow::NewScene()
    {
        return std::make_shared<MphRead::Scene>(FramebufferSize(), _window->Keyboard(), _window->Mouse(),
            [this](std::string title) { _window->Title(std::move(title)); },
            [this]() { EndOrClose(); });
    }

    void RenderWindow::EndOrClose()
    {
#if defined(MPHREAD_SHELL)
        if (_shell)
        {
            Mods::Launcher::Gui::Shell::RequestEndMatch();
            return;
        }
#endif
        _window->Close();
    }

    std::shared_ptr<MphRead::Scene> RenderWindow::NewSideScene()
    {
        std::shared_ptr<MphRead::Scene> scene = NewScene();
        scene->Size(FramebufferSize());
        scene->SideScene(true);
        return scene;
    }

    MphRead::Scene& RenderWindow::BeginScene()
    {
        _scene = NewScene();
        _scene->Size(FramebufferSize());
        _sceneLoaded = false;
        return *_scene;
    }

    void RenderWindow::LoadScene()
    {
        if (_scene == nullptr || _sceneLoaded)
        {
            return;
        }
        _scene->OnLoad();
        _sceneLoaded = true;
        _scene->OnResize();
    }

    void RenderWindow::EndScene()
    {
        if (_scene == nullptr)
        {
            return;
        }
        _scene->DoCleanup();
        _scene->ReleaseGpuResources();
        _scene.reset();
        _sceneLoaded = false;
        NativeRuntime::ForceFullGc();
    }

    void RenderWindow::FeedKey(const RendererPlatform::KeyboardKeyEventArgs& e)
    {
        OnKeyDown(e);
    }

    std::pair<double, double> RenderWindow::PointerPixels(double x, double y) const
    {
        const Vector2i client = ClientSize();
        const Vector2i framebuffer = FramebufferSize();
        const double scaleX = client.X > 0 ? framebuffer.X / static_cast<double>(client.X) : 1;
        const double scaleY = client.Y > 0 ? framebuffer.Y / static_cast<double>(client.Y) : 1;
        return {x * scaleX, y * scaleY};
    }

    bool RenderWindow::OnWayland()
    {
        if (!::MphRead::NativeRuntime::IsLinux())
        {
            return false;
        }
        auto session = ::MphRead::NativeRuntime::EnvironmentGetVariable("XDG_SESSION_TYPE");
        if (!session.has_value())
        {
            return false;
        }
        if (!NativeRuntime::StringEqualsOrdinalIgnoreCase(*session, "wayland"))
        {
            return false;
        }
        const auto useWayland = ::MphRead::NativeRuntime::EnvironmentGetVariable("OPENTK_4_USE_WAYLAND");
        return !useWayland.has_value() || *useWayland != "0";
    }

    void RenderWindow::IgnoreUnavailableGlfwFeatures()
    {
        if (_glfwErrorCallback)
        {
            return;
        }
        _glfwErrorCallback = [](std::int32_t code, std::string description)
        {
            if (code == RendererPlatform::GlfwFeatureUnavailableCode())
            {
                Mods::DebugLog::Line("window", "glfw feature unavailable, ignored: " + description);
                return;
            }
            throw RendererPlatform::GLFWException(std::move(description), code);
        };
        RendererPlatform::InstallGlfwErrorCallback(_glfwErrorCallback);
    }

    void RenderWindow::FitToScreen()
    {
        Vector2i floor = _minimumSize;
        if (OnWayland())
        {
            Mods::DebugLog::Line("window", "wayland session: keeping the fixed size floor");
            _window->MinimumSize(floor);
            return;
        }
        try
        {
            const Vector2i area = RendererPlatform::WorkAreaForWindow(*_window);
            const Vector2i room(std::max(320, area.X - 16), std::max(240, area.Y - 64));
            floor = Vector2i(std::min(floor.X, room.X), std::min(floor.Y, room.Y));
        }
        catch (const std::exception& ex)
        {
            Mods::DebugLog::Line("window", std::string("could not size against the display: ") + ex.what());
        }
        _window->MinimumSize(floor);
    }

    void RenderWindow::Run()
    {
        for (;;)
        {
            _window->Run(*this);
            if (!_rendererSwitch.has_value()) break;
            const NativeRuntime::Rhi::SceneBackendRequest request = *_rendererSwitch;
            _rendererSwitch.reset();
            SwitchRenderer(request);
        }
    }

    void RenderWindow::OnClosing()
    {
        if (_rendererSwitch.has_value())
        {
            // Not closing: the window is being remade for another renderer.
            _window->BaseOnClosing();
            return;
        }
        if (_shell)
        {
            Mods::WindowGeometry::Remember(*this);
            Mods::Launcher::LauncherPrefs::Save();
        }
        if (_scene != nullptr)
        {
            _scene->DoCleanup();
        }
        _window->BaseOnClosing();
    }

    void RenderWindow::AddRoom(std::int32_t id, GameMode mode, std::int32_t playerCount,
        BossFlags bossFlags, std::int32_t nodeLayerMask, std::int32_t entityLayerId)
    {
        const RoomMetadata* meta = Metadata::GetRoomById(id);
        if (meta == nullptr)
        {
            throw ProgramException("No room with this ID is known.");
        }
        _scene->AddRoom(meta->Name, mode, playerCount, bossFlags, nodeLayerMask, entityLayerId);
    }

    void RenderWindow::AddRoom(std::string name, GameMode mode, std::int32_t playerCount,
        BossFlags bossFlags, std::int32_t nodeLayerMask, std::int32_t entityLayerId)
    {
        _scene->AddRoom(std::move(name), mode, playerCount, bossFlags, nodeLayerMask, entityLayerId);
    }

    void RenderWindow::AddModel(std::string name, std::int32_t recolor, bool firstHunt,
        MetaDir dir, std::optional<Vector3> pos)
    {
        (void)_scene->AddModel(std::move(name), recolor, firstHunt, dir, pos);
    }

    void RenderWindow::AddPlayer(Hunter hunter, std::int32_t recolor, std::int32_t team,
        std::optional<Vector3> position)
    {
        _scene->AddPlayer(hunter, recolor, team, position);
    }

    void RenderWindow::QueueMovie(std::int32_t movieId)
    {
        Sound::Sfx::Load(*_scene);
        GameState::Mode(GameMode::Unknown15);
        _scene->StartMovie(static_cast<Movie>(movieId), FadeType::FadeOutInBlack, 0.0F,
            FadeType::FadeOutBlack, 0.0F, AfterMovie::EndGame);
    }

    void RenderWindow::OnLoad()
    {
        Mods::Input::WindowsPenInput::Attach(
            static_cast<GLFWwindow*>(_window->NativeHandle()));
        if (_scene != nullptr && !_sceneLoaded)
        {
            _scene->OnLoad();
            _sceneLoaded = true;
        }
        _window->BaseOnLoad();
    }

    bool RenderWindow::BeforeFrame()
    {
        namespace Rhi = NativeRuntime::Rhi;
        ApplyFrameRateSettings();
        const auto size = FramebufferSize();
        if (size.X <= 0 || size.Y <= 0)
        {
            _swapchain->AbandonLowLatencyFrame();
            return true;
        }
        auto state = Rhi::ResolveLowLatency(Mods::Launcher::LauncherPrefs::LowLatency(), _swapchain->LowLatencyCaps());
        const bool nativePacing = state.authority == Rhi::PacingAuthority::Native;
        // Native measurement also opens a frame in Off; only pacing is disabled.
        if (!_swapchain->BeginLowLatencyFrame()) return false;
        state = Rhi::ResolveLowLatency(state.requested, _swapchain->LowLatencyCaps());
        if (state.effective == Rhi::LowLatencyMode::Off) return true;
        if (state.authority == Rhi::PacingAuthority::Native)
        {
            return true;
        }
        if (nativePacing) ApplyFrameRateSettings(); // Native failure hands this frame to Generic.
        return Rhi::SceneDevice().WaitForLatestSubmission(Rhi::PresentationScheduler::FrameBudgetWait.count());
    }

    bool RenderWindow::CanSampleInputWhileWaiting() const
    {
        return NativeRuntime::Rhi::ResolveLowLatency(Mods::Launcher::LauncherPrefs::LowLatency(),
            _swapchain->LowLatencyCaps()).authority != NativeRuntime::Rhi::PacingAuthority::Native;
    }

    void RenderWindow::OnInputSample()
    { _swapchain->MarkLowLatency(NativeRuntime::Rhi::LowLatencyMarker::InputSample); }

    void RenderWindow::OnInputEventsProcessed()
    {
        // Reuse the gameplay capture policy after key/focus/UI events and before
        // the platform latches accumulated motion into this simulation frame.
        UpdateCursorCapture();
    }

    void RenderWindow::UpdateCursorCapture()
    {
        if (!_scene)
        {
            _window->Cursor(RendererPlatform::CursorState::Normal);
            return;
        }
        const bool playerCamera = _scene->CameraMode() == MphRead::CameraMode::Player;
        const bool freeCamera = _scene->IsFreeCam();
        const bool frameAdvance = _scene->FrameAdvance();
        const bool pauseOpen = Mods::PauseMenu::Open();
        const bool endScreen = Mods::EndScreen::Available();
        const bool stylusMode = Mods::Input::PointerInput::StylusMode();
        const bool stylusPlacing = Mods::Input::StylusZone::Placing();
        const bool sceneShowsCursor = _scene->ShowCursor();
        const bool dialogPause = GameState::DialogPause();
        const bool menuPause = GameState::MenuPause();
        const bool focused = IsFocused();
        const bool grab = (playerCamera || freeCamera) && !frameAdvance && !pauseOpen && !endScreen
            && !stylusMode && !stylusPlacing && !sceneShowsCursor && !dialogPause && !menuPause
            && focused && !Mods::Chat::ChatBox::Composing();
        if (Mods::DebugLog::Active())
        {
            static std::optional<std::pair<bool, bool>> lastCursorState;
            const std::pair<bool, bool> cursorState{grab, focused};
            if (!lastCursorState.has_value() || *lastCursorState != cursorState)
            {
                const auto bit = [](bool value) { return value ? "1" : "0"; };
                Mods::DebugLog::Line("input", std::string("cursor grab=") + bit(grab)
                    + " focus=" + bit(focused) + " player=" + bit(playerCamera)
                    + " freecam=" + bit(freeCamera) + " frameadvance=" + bit(frameAdvance)
                    + " pause=" + bit(pauseOpen) + " end=" + bit(endScreen)
                    + " stylus=" + bit(stylusMode) + " stylusplacing=" + bit(stylusPlacing)
                    + " weaponwheel=" + bit(sceneShowsCursor) + " dialog=" + bit(dialogPause)
                    + " menupause=" + bit(menuPause));
                lastCursorState = cursorState;
            }
        }
        _window->Cursor(grab ? RendererPlatform::CursorState::Grabbed : RendererPlatform::CursorState::Normal);
    }

    void RenderWindow::ApplyFrameRateSettings()
    {
        const std::int32_t cap = Mods::Diagnostics::FramePerformance::EffectiveCap(Mods::Render::FrameTiming::FrameRateCap());
        const bool vsync = Mods::Render::FrameTiming::VSync();
        const auto latency = NativeRuntime::Rhi::ResolveLowLatency(Mods::Launcher::LauncherPrefs::LowLatency(),
            NativeRuntime::Rhi::SceneDevice().LowLatencyCaps());
        // With Reflex pacing, FIFO alone did not hold the frame rate to the
        // display (1136 FPS presented FIFO on a 540 Hz screen), so VSync also
        // gives Reflex the refresh period as its minimum interval -- the
        // driver-side cap NVIDIA describes for Reflex with VSync. A lower
        // FPS cap still wins.
        _swapchain->ConfigureLowLatency(Mods::Launcher::LauncherPrefs::LowLatency(),
            NativeRuntime::Rhi::ReflexMinimumIntervalUs(cap,
                vsync && latency.authority == NativeRuntime::Rhi::PacingAuthority::Native ? RefreshRate() : 0.0));
        if (!_reportedLatency || *_reportedLatency != latency)
        {
            _reportedLatency = latency;
            std::cout << "[presentation] requested_low_latency_mode=" << static_cast<int>(latency.requested)
                << " effective_low_latency_mode=" << static_cast<int>(latency.effective)
                << " low_latency_provider=" << static_cast<int>(latency.provider)
                << " boost_supported=" << latency.boostSupported
                << " pacing_authority=" << static_cast<int>(latency.authority)
                << " reason=" << latency.fallbackReason << '\n';
        }
        _window->PresentationTiming(_swapchain->Desc().presentMode, cap, latency.authority);
        // The swapchain's own request is checked as well as the settings: a
        // swapchain made after the last change starts at FIFO, and with VSync
        // off that pinned the frame rate to the refresh until a setting moved.
        const auto wanted = vsync ? NativeRuntime::Rhi::PresentMode::Fifo : NativeRuntime::Rhi::PresentMode::Immediate;
        if (cap == _appliedFrameRateCap && vsync == _appliedVSync && _swapchain->RequestedPresentMode() == wanted)
        {
            return;
        }
        if (Mods::DebugLog::Active() && cap == _appliedFrameRateCap && vsync == _appliedVSync)
        {
            Mods::DebugLog::Line("render", std::string("present mode drifted from the VSync setting; re-applying ")
                + (vsync ? "FIFO" : "Immediate"));
        }
        _appliedFrameRateCap = cap;
        _appliedVSync = vsync;
        _swapchain->SetPresentMode(wanted);
        _window->UpdateFrequency(0.0);
        _window->PresentationTiming(_swapchain->Desc().presentMode, cap, latency.authority);
    }

    void RenderWindow::Reveal()
    {
        if (_shell)
        {
            Mods::WindowGeometry::Flush();
        }
        if (_startedHidden)
        {
            _window->Visible(true);
            _startedHidden = false;
            _applyStartupIn = 3;
        }
        else if (_applyStartupIn > 0 && --_applyStartupIn == 0)
        {
            Mods::WindowMode::ApplyStartup(*this);
        }
    }

    void RenderWindow::ReportReflexPacing(const NativeRuntime::Rhi::LowLatencyDiagnostics& reflex)
    {
        // Which machine set this frame rate, on one line: the cap and the
        // interval it became, the present mode asked for and the one the
        // surface gave, the screen's rate, and where native admission spent
        // its time. The 265 FPS question is answered by reading this.
        namespace Rhi = NativeRuntime::Rhi;
        const auto state = Rhi::ResolveLowLatency(Mods::Launcher::LauncherPrefs::LowLatency(), _swapchain->LowLatencyCaps());
        const auto present = [](Rhi::PresentMode mode)
        { return mode == Rhi::PresentMode::Immediate ? "Immediate" : mode == Rhi::PresentMode::Mailbox ? "Mailbox" : "Fifo"; };
        const auto mode = [](Rhi::LowLatencyMode value)
        { return value == Rhi::LowLatencyMode::OnBoost ? "OnBoost" : value == Rhi::LowLatencyMode::On ? "On" : "Off"; };
        const auto provider = [](Rhi::LowLatencyProvider value)
        {
            return value == Rhi::LowLatencyProvider::Nvidia ? "Nvidia" : value == Rhi::LowLatencyProvider::Amd ? "Amd"
                : value == Rhi::LowLatencyProvider::Generic ? "Generic" : "None";
        };
        const std::int32_t cap = Mods::Diagnostics::FramePerformance::EffectiveCap(Mods::Render::FrameTiming::FrameRateCap());
        const auto& p = reflex.pacing;
        std::ostringstream line;
        line.imbue(std::locale::classic());
        line << std::fixed << std::setprecision(1)
             << "[reflex-pacing] mode=" << mode(state.requested) << " effective=" << mode(state.effective)
             << " provider=" << provider(state.provider)
             << " authority=" << (state.authority == Rhi::PacingAuthority::Native ? "Native" : "Generic")
             << " cap=" << (cap == -1 ? std::string("unlimited") : cap == 0 ? std::string("display") : std::to_string(cap))
             << " vsync=" << (Mods::Render::FrameTiming::VSync() ? "on" : "off")
             << " minimum_interval_us=" << reflex.minimumIntervalUs
             << " present_requested=" << present(_swapchain->RequestedPresentMode())
             << " present_actual=" << present(_swapchain->Desc().presentMode)
             << " screen_hz=" << RefreshRate()
             << " images=" << _swapchain->Desc().imageCount
             << " revision=" << reflex.revision
             << " frame=" << reflex.frameId
             << " completed=" << reflex.completedMeasurementFrames
             << " abandoned=" << reflex.abandonedMeasurementFrames
             << " samples=" << p.admission.samples
             << " sleep_call_us=" << p.sleepCall.p50 << '/' << p.sleepCall.p95 << '/' << p.sleepCall.p99 << '/' << p.sleepCall.max
             << " wait_us=" << p.wait.p50 << '/' << p.wait.p95 << '/' << p.wait.p99 << '/' << p.wait.max
             << " admission_us=" << p.admission.p50 << '/' << p.admission.p95 << '/' << p.admission.p99 << '/' << p.admission.max
             << " wait_timeouts=" << p.waitTimeouts;
        std::cout << line.str() << '\n';
    }

    NativeRuntime::Rhi::CommandList& RenderWindow::WindowCommands()
    {
        if (!_windowCommands)
        {
            auto commands = NativeRuntime::Rhi::SceneDevice().CreateCommandList();
            commands->Begin();
            _windowCommands = std::move(commands);
        }
        return *_windowCommands;
    }

    void RenderWindow::OnRenderFrame(const RendererPlatform::FrameEventArgs& args)
    {
        if (_performance) _performance->BeginFrame(*this, *_swapchain);
        const auto present = [&]
        {
            const NativeRuntime::FrameTelemetry::Scope measured(NativeRuntime::FrameTelemetry::Phase::Present);
            _swapchain->MarkLowLatency(NativeRuntime::Rhi::LowLatencyMarker::SimulationEnd);
            if (std::getenv("FRUITY_RENDER_METRICS") && ++_presentationMetricFrames % 120 == 0)
            {
                const auto waits = NativeRuntime::Rhi::SceneDevice().PresentationWaits();
                std::cout << "[presentation-metrics] present_wait_count=" << waits.count
                    << " present_wait_ns=" << waits.nanoseconds << '\n';
                const auto reflex = _swapchain->LowLatencyStats();
                std::cout << "[reflex-metrics] sleep=" << reflex.sleepCalls << " wait=" << reflex.waitCalls
                    << " modes=" << reflex.modeCalls << " markers=" << reflex.markerCalls << " reports=" << reflex.timingReports
                    << " frame=" << reflex.frameId << " generation=" << reflex.swapchainGeneration << '\n';
                ReportReflexPacing(reflex);
            }
            if (!_performance)
            {
                const auto result = NativeRuntime::Rhi::PresentSceneWindow(*_swapchain);
                if (result.accepted) { _window->PresentationAccepted(); NativeRuntime::FrameTelemetry::Rendered(); }
                else _window->PresentationUnavailable();
                return;
            }
            const auto start = std::chrono::steady_clock::now();
            const auto result = NativeRuntime::Rhi::PresentSceneWindow(*_swapchain);
            if (result.accepted) { _window->PresentationAccepted(); NativeRuntime::FrameTelemetry::Rendered(); }
            else _window->PresentationUnavailable();
            if (_performance) _performance->Presented(*this, *_swapchain,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        };
        if (Mods::Network::NetLaunch::TickTerminalLobby(*this))
        {
            {
                auto& commands = WindowCommands();
                std::array<NativeRuntime::Rhi::RenderingColorAttachment, 1> color{};
                color[0].loadOp = NativeRuntime::Rhi::LoadOp::Clear;
                color[0].clearValue = NativeRuntime::Rhi::ClearColor{0.0F, 0.0F, 0.0F, 1.0F};
                NativeRuntime::Rhi::RenderingInfo info{};
                info.swapchain = true;
                info.colorAttachments = color;
                commands.BeginRendering(info);
                commands.EndRendering();
            }
            present();
            _window->BaseOnRenderFrame(args);
            return;
        }
#if defined(MPHREAD_SHELL)
        Mods::Launcher::Gui::Shell::BeforeFrame(*this);
        Mods::Launcher::Gui::Shell::TickEndPanel();
        if (_scene == nullptr)
        {
            _swapchain->MarkLowLatency(NativeRuntime::Rhi::LowLatencyMarker::SimulationStart);
            _swapchain->MarkLowLatency(NativeRuntime::Rhi::LowLatencyMarker::SimulationEnd);
            // With no game scene the launcher owns the whole frame, so its
            // Ganesh pass may run before the launcher background/composite.
            Mods::Launcher::Gui::Shell::TickUi(*this);
            _window->Cursor(RendererPlatform::CursorState::Normal);
            Mods::Input::PointerDevice::Reset();
            const Vector2i framebuffer = FramebufferSize();
            Mods::Render::UiOverlay::DrawAlone(*this, framebuffer.X, framebuffer.Y);
            Mods::Launcher::Gui::Shell::AfterDraw(*this);
            present();
            Reveal();
            Mods::PauseMenu::Poll(*this);
            _window->BaseOnRenderFrame(args);
            return;
        }
#endif
        UpdateCursorCapture();
        const Vector2i clientSize = _window->ClientSize();
        const float pointerX = _window->Mouse().X / static_cast<float>(std::max(clientSize.X, 1));
        const float pointerY = _window->Mouse().Y / static_cast<float>(std::max(clientSize.Y, 1));
        Mods::EndScreen::NotePointer(pointerX, pointerY);
        bool independentPrimary = false;
        const Mods::Input::PointerSample pointer = Mods::Input::WindowsPenInput::Read(
            _window->Mouse(), clientSize.X, clientSize.Y, independentPrimary);
        Mods::Input::PointerDevice::Update(pointer, clientSize.X, clientSize.Y, independentPrimary,
            IsFocused() && !Mods::PauseMenu::Open() && !Mods::Chat::ChatBox::Composing()
                && !GameState::MenuPause() && !GameState::DialogPause() && !Mods::EndScreen::Available());
        if (Mods::Input::StylusZone::Placing())
        {
            Mods::Input::StylusZone::PlacementDrag(pointerX, pointerY);
        }
        GameState::ApplyPause();
        _swapchain->MarkLowLatency(NativeRuntime::Rhi::LowLatencyMarker::SimulationStart);
        std::int32_t steps;
        if (_scene->FrameAdvance())
        {
            Mods::Render::FrameTiming::Reset();
            steps = 1;
        }
        else
        {
            steps = Mods::Render::FrameTiming::Advance(args.Time);
        }
        for (std::int32_t i = 0; i < steps; ++i)
        {
            const NativeRuntime::FrameTelemetry::Scope measured(NativeRuntime::FrameTelemetry::Phase::Simulation);
            _scene->OnSimulationFrame();
        }
        _swapchain->MarkLowLatency(NativeRuntime::Rhi::LowLatencyMarker::SimulationEnd);
        if (Mods::Chat::ChatBox::Composing()
            && Mods::Input::GamepadInput::TakePress(
                Mods::Input::GamepadButtons::B | Mods::Input::GamepadButtons::Start))
        {
            Mods::Chat::ChatBox::Cancel();
        }
        if (Mods::Input::GamepadInput::TakeMenuPress()
            && (_scene->CameraMode() == MphRead::CameraMode::Player || _scene->IsFreeCam()))
        {
            (void)Mods::PauseMenu::HandleEscape(*this);
        }
        if (Mods::Input::GamepadInput::TakeChatPress()
            && Mods::Chat::ChatBox::Available() && !Mods::Chat::ChatBox::Composing())
        {
            Mods::Chat::ChatBox::Open(false);
        }
        {
            const NativeRuntime::FrameTelemetry::Scope measured(NativeRuntime::FrameTelemetry::Phase::Traversal);
            _scene->OnDrawFrame();
        }
        if (!_scene->OnRenderFrame())
        {
            return;
        }
#if defined(MPHREAD_SHELL)
        // Never let Skia/Ganesh run before the game's OpenGL scene. The UI
        // shares the window context only as a compositor backend; map/model/
        // HUD rendering above remains entirely on the existing OpenGL path.
        // When the UI is hidden TickUi performs no Ganesh render at all.
        {
            const NativeRuntime::FrameTelemetry::Scope measured(NativeRuntime::FrameTelemetry::Phase::Ui);
            Mods::Launcher::Gui::Shell::TickUi(*this);
            const Vector2i framebuffer = FramebufferSize();
            Mods::Render::UiOverlay::Draw(framebuffer.X, framebuffer.Y);
            Mods::Render::LauncherHunter::Draw(*this, framebuffer.X, framebuffer.Y);
            Mods::Launcher::Gui::Shell::AfterDraw(*this);
        }
#endif
        present();
        Reveal();
        Mods::PauseMenu::Poll(*this);
        _scene->AfterRenderFrame();
        _window->BaseOnRenderFrame(args);
    }

    void RenderWindow::OnResize(const RendererPlatform::ResizeEventArgs& e)
    {
        if (_shell)
        {
            Mods::WindowGeometry::Note(*this);
        }
        if (e.Size.X > 0 && e.Size.Y > 0 && _swapchain != nullptr)
        {
            _swapchain->Resize(
                static_cast<std::uint32_t>(e.Size.X),
                static_cast<std::uint32_t>(e.Size.Y));
        }
        if (!_sceneReady)
        {
            return;
        }
        if (e.Size.X <= 0 || e.Size.Y <= 0)
        {
            _window->BaseOnResize(e);
            return;
        }
        WindowCommands().SetViewport(NativeRuntime::Rhi::Viewport{0.0F, 0.0F,
            static_cast<float>(e.Size.X), static_cast<float>(e.Size.Y)});
        if (_scene != nullptr && _scene->Size() != e.Size)
        {
            _scene->Size(e.Size);
            _scene->OnResize();
        }
        _window->BaseOnResize(e);
    }

    void RenderWindow::OnMove(const RendererPlatform::WindowPositionEventArgs& e)
    {
        if (_shell)
        {
            Mods::WindowGeometry::Note(*this);
        }
        _window->BaseOnMove(e);
    }

    void RenderWindow::OnMaximizedChanged(bool maximized)
    {
        (void)maximized;
        if (_shell)
        {
            Mods::WindowGeometry::Note(*this);
        }
        _window->BaseOnMaximizedChanged(maximized);
    }

    void RenderWindow::OnFocusedChanged(bool focused)
    {
        // Focus can leave and return between polls (external capture/Alt+Tab).
        // Keep the cached fullscreen state and platform flag in one owner.
        Mods::WindowMode::SetTopmost(*this, Mods::WindowMode::IsFullscreen() && focused);
        Mods::Input::GamepadContexts::Focused(focused);
        if (!focused)
        {
            Mods::Input::GamepadManager::ClearAll();
            Mods::Input::GamepadHaptics::Stop();
        }
        _window->BaseOnFocusedChanged(focused);
    }

    void RenderWindow::OnMouseDown(const RendererPlatform::MouseButtonEventArgs& e)
    {
        Mods::Input::InputSourceTracker::Note(Mods::Input::InputSource::KeyboardMouse);
#if defined(MPHREAD_SHELL)
        if (Mods::Launcher::Gui::Shell::UiVisible())
        {
            const auto [x, y] = PointerPixels(_window->Mouse().X, _window->Mouse().Y);
            Mods::Launcher::Gui::Shell::PointerButton(e.Button, x, y, true);
            _window->BaseOnMouseDown(e);
            return;
        }
#endif
        if (e.Button == RendererPlatform::MouseButton::Button1)
        {
            const Vector2i size = _window->ClientSize();
            if (Mods::Input::StylusZone::Placing())
            {
                Mods::Input::StylusZone::PlacementDown(
                    _window->Mouse().X / static_cast<float>(std::max(size.X, 1)),
                    _window->Mouse().Y / static_cast<float>(std::max(size.Y, 1)));
                _window->BaseOnMouseDown(e);
                return;
            }
            if (Mods::Network::MapVote::HandleClick())
            {
                _window->BaseOnMouseDown(e);
                return;
            }
            if (Mods::EndScreen::HandleClick())
            {
                _window->BaseOnMouseDown(e);
                return;
            }
            if (Mods::SpectatorMode::IsSpectating())
            {
                Mods::SpectatorMode::CycleNext();
            }
            else
            {
                _scene->OnMouseClick(true);
            }
        }
        _window->BaseOnMouseDown(e);
    }

    void RenderWindow::OnMouseUp(const RendererPlatform::MouseButtonEventArgs& e)
    {
#if defined(MPHREAD_SHELL)
        if (Mods::Launcher::Gui::Shell::UiVisible())
        {
            const auto [x, y] = PointerPixels(_window->Mouse().X, _window->Mouse().Y);
            Mods::Launcher::Gui::Shell::PointerButton(e.Button, x, y, false);
            _window->BaseOnMouseUp(e);
            return;
        }
#endif
        if (e.Button == RendererPlatform::MouseButton::Button1)
        {
            if (Mods::Input::StylusZone::Placing())
            {
                Mods::Input::StylusZone::PlacementUp();
                Mods::Chat::ChatBox::System("pen zone set");
                _window->BaseOnMouseUp(e);
                return;
            }
            _scene->OnMouseClick(false);
        }
        _window->BaseOnMouseUp(e);
    }

    void RenderWindow::OnMouseMove(const RendererPlatform::MouseMoveEventArgs& e)
    {
        if (std::abs(e.DeltaX) + std::abs(e.DeltaY) > 2)
        {
            Mods::Input::InputSourceTracker::Note(Mods::Input::InputSource::KeyboardMouse);
        }
#if defined(MPHREAD_SHELL)
        if (Mods::Launcher::Gui::Shell::UiVisible())
        {
            const auto [x, y] = PointerPixels(e.X, e.Y);
            Mods::Launcher::Gui::Shell::PointerMoved(x, y);
            _window->BaseOnMouseMove(e);
            return;
        }
#endif
        // Filtered for the same reason the player's aim is: the free
        // camera is reached from a match, with the same pointer.
        if (!IsFocused())
        {
            _window->BaseOnMouseMove(e);
            return;
        }
        const auto [deltaX, deltaY] = _scene->IsFreeCam()
            ? Mods::Input::PointerInput::Filter(e.DeltaX, e.DeltaY) : std::pair<float, float>(e.DeltaX, e.DeltaY);
        _scene->OnMouseMove(deltaX, deltaY);
        _window->BaseOnMouseMove(e);
    }

    void RenderWindow::OnMouseWheel(const RendererPlatform::MouseWheelEventArgs& e)
    {
#if defined(MPHREAD_SHELL)
        // First: the results screen's ballot is a Skia panel now, and handing
        // the wheel to the HUD picker left its ScrollViewer unreachable.
        if (Mods::Launcher::Gui::Shell::UiVisible())
        {
            Mods::Launcher::Gui::Shell::PointerWheel(e.OffsetX, e.OffsetY);
            _window->BaseOnMouseWheel(e);
            return;
        }
#endif
        if (Mods::MapPick::Available() && e.OffsetY != 0)
        {
            Mods::MapPick::Wheel(e.OffsetY > 0 ? -1 : 1);
            _window->BaseOnMouseWheel(e);
            return;
        }
        _scene->OnMouseWheel(e.OffsetY);
        _window->BaseOnMouseWheel(e);
    }

    void RenderWindow::OnTextInput(const RendererPlatform::TextInputEventArgs& e)
    {
#if defined(MPHREAD_SHELL)
        if (Mods::Launcher::Gui::Shell::UiVisible())
        {
            const std::u32string codePoint(1, static_cast<char32_t>(e.Unicode));
            Mods::Launcher::Gui::Shell::TextInput(
                NativeRuntime::Utf32ToUtf8(codePoint));
            _window->BaseOnTextInput(e);
            return;
        }
#endif
        Mods::Chat::ChatBox::HandleText(e.Unicode);
        _window->BaseOnTextInput(e);
    }

    void RenderWindow::OnKeyUp(const RendererPlatform::KeyboardKeyEventArgs& e)
    {
#if defined(MPHREAD_SHELL)
        if (Mods::Launcher::Gui::Shell::UiVisible())
        {
            Mods::Launcher::Gui::Shell::KeyUp(e);
        }
#endif
        _window->BaseOnKeyUp(e);
    }

    void RenderWindow::OnKeyDown(const RendererPlatform::KeyboardKeyEventArgs& e)
    {
        Mods::Input::InputSourceTracker::Note(Mods::Input::InputSource::KeyboardMouse);
#if defined(MPHREAD_SHELL)
        if (Mods::Launcher::Gui::Shell::UiVisible()
            && !Mods::Input::KeyCapture::AnyListening()
            && Mods::WindowMode::HandleKey(*this, e))
        {
            _window->BaseOnKeyDown(e);
            return;
        }
        if (Mods::Launcher::Gui::Shell::UiVisible())
        {
            Mods::Launcher::Gui::Shell::KeyDown(e);
            _window->BaseOnKeyDown(e);
            return;
        }
#endif
        using RendererPlatform::Key;
        if (Mods::Chat::ChatBox::HandleKeyDown(e,
            !Mods::Network::DemoPlayback::IsActive()
                && (_scene->CameraMode() == MphRead::CameraMode::Player || _scene->IsFreeCam())))
        {
            _window->BaseOnKeyDown(e);
            return;
        }
        if (Mods::EndScreen::HandleKeyDown(e.Key))
        {
            _window->BaseOnKeyDown(e);
            return;
        }
        if (e.Key == RendererPlatform::EscapeKey && Mods::Input::StylusZone::Placing())
        {
            Mods::Input::StylusZone::CancelPlacement();
            Mods::Chat::ChatBox::System("pen zone left as it was");
            _window->BaseOnKeyDown(e);
            return;
        }
        if ((e.Key == RendererPlatform::F1Key || e.Key == RendererPlatform::F2Key) && !e.Alt && !e.Control
            && Mods::Network::MapVote::Active() && !Mods::Network::MapVote::Answered())
        {
            Mods::Network::MapVote::Cast(e.Key == RendererPlatform::F1Key);
            _window->BaseOnKeyDown(e);
            return;
        }
        if (e.Key != Key::Unknown && e.Key == Mods::InputSettings::ClipKey()
            && !e.Alt && !e.Control && Mods::Network::DemoClip::Active())
        {
            const double held = Mods::Network::DemoClip::Held();
            auto clip = Mods::Network::DemoClip::Save();
            if (clip.has_value())
            {
                Mods::Chat::ChatBox::System("saved the last " + NativeRuntime::ToString(held, "0") + " s to "
                    + NativeRuntime::PathGetFileName(*clip));
            }
            else
            {
                Mods::Chat::ChatBox::System("nothing to clip yet");
            }
            _window->BaseOnKeyDown(e);
            return;
        }
        if (Mods::WindowMode::HandleKey(*this, e))
        {
            _window->BaseOnKeyDown(e);
            return;
        }
        if (e.Key == Key::Space
            && (Mods::Network::DemoPlayback::IsActive() || Mods::SpectatorMode::IsSpectating()))
        {
            if (Mods::Network::DemoPlayback::IsActive())
            {
                _scene->ToggleFreeCamera();
            }
            else
            {
                Mods::SpectatorMode::ToggleView();
            }
            _window->BaseOnKeyDown(e);
            return;
        }
        if (e.Key == RendererPlatform::EscapeKey
            && (_scene->CameraMode() == MphRead::CameraMode::Player || _scene->IsFreeCam())
            && Mods::PauseMenu::HandleEscape(*this))
        {
            _window->BaseOnKeyDown(e);
            return;
        }
        if (e.Key == RendererPlatform::EscapeKey)
        {
            _scene->DoCleanup();
            if (GameState::SinglePlayer())
            {
                Menu::NeededSave = Menu::SaveFromExit;
            }
            EndOrClose();
        }
        else
        {
            _scene->OnKeyDown(e);
        }
        _window->BaseOnKeyDown(e);
    }

#endif

    std::int32_t TextureMap::GetKey(std::int32_t textureId, std::int32_t paletteId,
        std::int32_t recolorId) const
    {
        if (paletteId == -1)
        {
            paletteId = 4095;
        }
        MPHREAD_DEBUG_ASSERT(textureId >= 0 && textureId < 4096);
        MPHREAD_DEBUG_ASSERT(paletteId >= 0 && paletteId < 4096);
        MPHREAD_DEBUG_ASSERT(recolorId >= 0 && recolorId < 255);
        const std::uint32_t key = static_cast<std::uint32_t>(textureId)
            | (static_cast<std::uint32_t>(paletteId) << 12U)
            | (static_cast<std::uint32_t>(recolorId) << 24U);
        return std::bit_cast<std::int32_t>(key);
    }

    std::optional<std::size_t> TextureMap::FindIndex(std::int32_t key) const noexcept
    {
        for (std::size_t i = 0; i < _items.size(); ++i)
        {
            if (_items[i].first == key)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    TextureMapValue TextureMap::GetItem(std::int32_t key) const
    {
        if (auto index = FindIndex(key))
        {
            return _items[*index].second;
        }
        throw SceneDetail::KeyNotFoundException();
    }

    void TextureMap::SetItem(std::int32_t key, TextureMapValue value)
    {
        if (auto index = FindIndex(key))
        {
            _items[*index].second = std::move(value);
            return;
        }
        _items.emplace_back(key, std::move(value));
    }

    TextureMapValue TextureMap::Get(std::int32_t textureId, std::int32_t paletteId,
        std::int32_t recolorId) const
    {
        return GetItem(GetKey(textureId, paletteId, recolorId));
    }

    void TextureMap::Add(std::int32_t textureId, std::int32_t paletteId,
        std::int32_t recolorId, std::int32_t bindingId, bool onlyOpaque)
    {
        SetItem(GetKey(textureId, paletteId, recolorId), TextureMapValue{bindingId, onlyOpaque});
    }

#undef MPHREAD_DEBUG_ASSERT

#if !defined(__ANDROID__)
    std::int32_t RenderWindow::WindowBorder() const
    {
        return _window->WindowBorder();
    }

    void RenderWindow::WindowBorder(std::int32_t value)
    {
        _window->WindowBorder(value);
    }

    OpenTK::Mathematics::Vector2i RenderWindow::Location() const
    {
        return _window->Location();
    }

    void RenderWindow::Location(OpenTK::Mathematics::Vector2i value)
    {
        _window->Location(value);
    }

    OpenTK::Mathematics::Vector2i RenderWindow::ClientSize() const
    {
        return _window->ClientSize();
    }

    void RenderWindow::ClientSize(OpenTK::Mathematics::Vector2i value)
    {
        _window->ClientSize(value);
    }

    RendererPlatform::MonitorArea RenderWindow::CurrentMonitorClientArea() const
    {
        return _window->CurrentMonitorClientArea();
    }

    std::vector<RendererPlatform::MonitorArea> RenderWindow::MonitorClientAreas() const
    {
        return _window->MonitorClientAreas();
    }

    RendererPlatform::WindowStateValue RenderWindow::WindowState() const
    {
        return _window->WindowState();
    }

    void RenderWindow::WindowStateMinimized()
    {
        _window->WindowStateMinimized();
    }

    void RenderWindow::WindowStateMaximized()
    {
        _window->WindowStateMaximized();
    }

    void RenderWindow::WindowStateNormal()
    {
        _window->WindowStateNormal();
    }

    bool RenderWindow::WindowStateFullscreen()
    {
        return _window->WindowStateFullscreen();
    }

    bool RenderWindow::WindowStateBorderless()
    {
        return _window->WindowStateBorderless();
    }

    double RenderWindow::RefreshRate() const
    {
        return _window->RefreshRate();
    }

    void RenderWindow::Floating(bool value)
    {
        _window->Floating(value);
    }

    bool RenderWindow::IsFocused() const
    {
        return _window->IsFocused();
    }

    OpenTK::Mathematics::Vector2i RenderWindow::ClientLocation() const
    {
        return _window->ClientLocation();
    }

    void RenderWindow::Focus()
    {
        _window->Focus();
    }

    void RenderWindow::Close()
    {
        _window->Close();
    }
#endif

}
