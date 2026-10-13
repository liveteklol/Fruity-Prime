#include "GameState.hpp"

#include "Menu.hpp"
#include "NativeRuntime/System/Json.hpp"
#include "Mods/DebugLog.hpp"
#include "Mods/Headless.hpp"
#include "Mods/MapPick.hpp"
#include "Mods/Multiplayer/TeamVisuals.hpp"
#include "Mods/Network/Session/NetMatchEnd.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#if defined(MPHREAD_SHELL)
#include "Mods/Launcher/Shell.hpp"
#endif
#include "NativeRuntime/System/IO.hpp"

#include "NativeRuntime/System/Enum.hpp"

#include "Features.hpp"
#include "Messaging.hpp"
#include "Scene.hpp"
#include "Strings.hpp"
#include "Entities/CamSeq/CamSeqEntity.hpp"
#include "Entities/CamSeq/CameraSequence.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Metadata/Player.hpp"
#include "Sound/Music.hpp"
#include "Sound/Sfx.hpp"
#include "Formats/Types.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::ShiftRight;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::MphRead::NativeRuntime::UncheckedSubtract;

namespace MphRead::GameStateDetail
{
    // Defined at the end of this file, beside the rest of GameState.cs's
    // serialization.
    [[nodiscard]] std::shared_ptr<StorySave> DeserializeStorySave(const std::string& json);
    [[nodiscard]] std::string SerializeStorySave(const std::shared_ptr<StorySave>& save);
    void DeserializeSettings(const std::string& json,
        std::shared_ptr<const std::unordered_map<std::string, std::string>>& features,
        std::shared_ptr<MenuSettings>& menuSettings);
    [[nodiscard]] std::string SerializeSettings(
        const std::unordered_map<std::string, std::string>& features,
        const std::shared_ptr<MenuSettings>& menuSettings);
    [[nodiscard]] std::shared_ptr<MenuSettings> NewMenuSettings();
}

namespace MphRead
{
    enum class AfterFade : std::int32_t;
    enum class AfterMovie : std::int32_t;
    enum class Movie : std::int32_t;
}

namespace
{
    template <typename T>
    [[nodiscard]] const std::shared_ptr<T>& RequireShared(const std::shared_ptr<T>& value)
    {
        if (!value)
        {
            throw System::NullReferenceException();
        }
        return value;
    }

    template <typename T>
    void CopyManagedArray(const std::shared_ptr<MphRead::ManagedArray<T>>& source,
        const std::shared_ptr<MphRead::ManagedArray<T>>& destination,
        std::size_t length, const char* destinationParam)
    {
        const auto& sourceArray = RequireShared(source);
        if (!destination)
        {
            throw System::ArgumentNullException(destinationParam);
        }
        const auto& destinationArray = destination;
        if (length > sourceArray->Length() || length > destinationArray->Length())
        {
            throw std::invalid_argument("Destination array was not long enough.");
        }
        for (std::size_t i = 0; i < length; ++i)
        {
            (*destinationArray)[i] = (*sourceArray)[i];
        }
    }

    // {value:X}: uppercase hexadecimal, no leading zeros.
    [[nodiscard]] std::string HexUpper(std::uint32_t value)
    {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string text;
        do
        {
            text.insert(text.begin(), digits[value & 15U]);
            value >>= 4;
        } while (value != 0);
        return text;
    }

    [[nodiscard]] std::int32_t ShiftLeftInt32(
        std::int32_t value, std::int32_t count) noexcept
    {
        const std::uint32_t shift = static_cast<std::uint32_t>(count) & 31U;
        return std::bit_cast<std::int32_t>(
            std::bit_cast<std::uint32_t>(value) << shift);
    }

    [[nodiscard]] bool HasLoadFlag(
        MphRead::Entities::LoadFlags value, MphRead::Entities::LoadFlags flag) noexcept
    {
        return (value & flag) == flag;
    }

    [[nodiscard]] bool HasCamFlag(
        MphRead::Formats::CamSeqFlags value, MphRead::Formats::CamSeqFlags flag) noexcept
    {
        return (static_cast<std::int32_t>(value) & static_cast<std::int32_t>(flag))
            == static_cast<std::int32_t>(flag);
    }

    [[nodiscard]] MphRead::Entities::PlayerEntity* MainPlayer()
    {
        return &RequireReference(MphRead::Entities::PlayerEntity::Main());
    }

    [[nodiscard]] MphRead::Sound::SfxInstanceBase* SfxInstance()
    {
        return RequireShared(MphRead::Sound::Sfx::Instance()).get();
    }

    [[nodiscard]] std::shared_ptr<MphRead::Entities::PlayerEntity> PlayerAt(
        std::int32_t index)
    {
        const auto& players = MphRead::Entities::PlayerEntity::Players();
        if (index < 0 || static_cast<std::size_t>(index) >= players.size())
        {
            throw MphRead::SceneDetail::IndexOutOfRangeException();
        }
        const auto& player = players[static_cast<std::size_t>(index)];
        RequireShared(player);
        return player;
    }

    struct TimeSpanParts
    {
        double TotalMinutes;
        std::int32_t Seconds;
    };

    [[nodiscard]] TimeSpanParts TimeSpanFromSeconds(float seconds)
    {
        const double ticksValue = static_cast<double>(seconds) * 10'000'000.0;
        if (std::isnan(ticksValue))
        {
            throw std::invalid_argument("TimeSpan cannot accept NaN.");
        }
        if (ticksValue > static_cast<double>(std::numeric_limits<std::int64_t>::max())
            || ticksValue < static_cast<double>(std::numeric_limits<std::int64_t>::min()))
        {
            throw std::overflow_error("TimeSpan overflow.");
        }
        // .NET 9 TimeSpan.IntervalFromDoubleTicks converts double to Int64,
        // which truncates toward zero.
        const std::int64_t ticks = static_cast<std::int64_t>(ticksValue);
        const std::int64_t wholeSeconds = ticks / 10'000'000;
        return TimeSpanParts{
            static_cast<double>(ticks) / 600'000'000.0,
            static_cast<std::int32_t>(wholeSeconds % 60)
        };
    }

    void OrCamFlag(MphRead::Formats::CameraSequence* sequence,
        MphRead::Formats::CamSeqFlags flag)
    {
        (void)RequireReference(sequence);
        sequence->Flags(static_cast<MphRead::Formats::CamSeqFlags>(
            static_cast<std::int32_t>(sequence->Flags())
            | static_cast<std::int32_t>(flag)));
    }

    void ReplaceBossFlag(MphRead::BossFlags& value,
        MphRead::BossFlags killFlag, MphRead::BossFlags doneFlag)
    {
        const std::uint32_t bits = static_cast<std::uint32_t>(value);
        const std::uint32_t kill = static_cast<std::uint32_t>(killFlag);
        if ((bits & kill) != 0)
        {
            value = static_cast<MphRead::BossFlags>(
                (bits & ~kill) | static_cast<std::uint32_t>(doneFlag));
        }
    }
}

namespace MphRead
{
    GameMode GameState::_mode = GameMode::SinglePlayer;
    bool GameState::_pausePrevented = false;
    bool GameState::_menuPause = false;
    bool GameState::_dialogPause = false;
    MatchState GameState::_matchState = MatchState::InProgress;
    TransitionState GameState::_transitionState = TransitionState::None;
    EscapeState GameState::_escapeState = EscapeState::None;
    float GameState::_escapeTimer = -1.0F;
    bool GameState::_escapePaused = false;

    GameState::IntSlots GameState::_encounterState{};
    GameState::BoolRooms GameState::_completedRandomEncounterRooms{};
    std::int32_t GameState::_transitionRoomId = -1;
    bool GameState::_transitionAltForm = false;
    std::int32_t GameState::_activePlayers = 0;
    GameState::NicknameSlots GameState::_nicknames = GameState::BuildDefaultNicknames();
    GameState::IntSlots GameState::_stars{};
    GameState::IntSlots GameState::_standings{};
    GameState::IntSlots GameState::_teamStandings{};
    GameState::IntSlots GameState::_resultSlots{};
    std::int32_t GameState::_primeHunter = -1;

    bool GameState::_teams = false;
    std::int32_t GameState::_teamCount = 2;
    bool GameState::_friendlyFire = false;
    std::int32_t GameState::_pointGoal = 0;
    float GameState::_timeGoal = 0.0F;
    bool GameState::_octolithReset = false;
    bool GameState::_radarPlayers = false;
    bool GameState::_affinityWeapons = false;
    bool GameState::_shadowFreeze = true;
    float GameState::_matchTime = -1.0F;
    bool GameState::_forceEndGame = false;

    GameState::IntSlots GameState::_points{};
    GameState::IntSlots GameState::_teamPoints{};
    GameState::IntSlots GameState::_kills{};
    GameState::IntSlots GameState::_teamKills{};
    GameState::IntSlots GameState::_deaths{};
    GameState::IntSlots GameState::_teamDeaths{};
    GameState::FloatSlots GameState::_time{};
    GameState::FloatSlots GameState::_teamTime{};
    GameState::IntSlots GameState::_beamDamageMax{};
    GameState::IntSlots GameState::_beamDamageDealt{};
    GameState::IntSlots GameState::_damageCount{};
    GameState::IntSlots GameState::_altDamageCount{};
    GameState::IntSlots GameState::_killStreak{};
    GameState::IntSlots GameState::_suicides{};
    GameState::IntSlots GameState::_friendlyKills{};
    GameState::IntSlots GameState::_headshotKills{};
    GameState::BeamKillSlots GameState::_beamKills{};
    GameState::IntSlots GameState::_octolithScores{};
    GameState::IntSlots GameState::_octolithDrops{};
    GameState::IntSlots GameState::_octolithStops{};
    GameState::IntSlots GameState::_nodesCaptured{};
    GameState::IntSlots GameState::_nodesLost{};
    GameState::IntSlots GameState::_killsAsPrime{};
    GameState::IntSlots GameState::_primesKilled{};

    GameState::ModeStateAction GameState::_modeState = &GameState::ModeStateAdventure;
    bool GameState::_pausingDialog = false;
    bool GameState::_unpausingDialog = false;

    bool GameState::_tempoChanged = false;
    bool GameState::_stateChanged = false;
    float GameState::_matchEndTime = 0.0F;
    float GameState::_lastAlarmTime = 0.0F;
    std::int32_t GameState::_nextAlarmIndex = 0;
    const std::array<float, 4> GameState::_alarmIntervals{
        1.0F / 30.0F, 8.0F / 30.0F, 15.0F / 30.0F, 6.0F / 30.0F};

    bool GameState::_whiteoutStarted = false;
    bool GameState::_gameOverShown = false;
    std::int32_t GameState::_queuedOctolithMessageId = -1;
    bool GameState::_queuedOublietteUnlockMessage = false;
    bool GameState::_playedTimedEventSfx = false;

    std::shared_ptr<StorySave> GameState::_cleanStorySave{};
    std::shared_ptr<StorySave> GameState::StorySave{};

    GameState::NicknameSlots GameState::BuildDefaultNicknames()
    {
        NicknameSlots names{};
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(names.size()); ++i)
        {
            names[static_cast<std::size_t>(i)] = "Player" + std::to_string(i + 1);
        }
        return names;
    }

    GameMode GameState::Mode() noexcept { return _mode; }
    void GameState::Mode(GameMode value) noexcept { _mode = value; }
    bool GameState::SinglePlayer()
    {
        return _mode == GameMode::SinglePlayer;
    }
    bool GameState::Multiplayer() { return !SinglePlayer(); }
    bool GameState::IsOctolithMode()
    {
        return _mode == GameMode::Capture
            || _mode == GameMode::Bounty
            || _mode == GameMode::BountyTeams;
    }
    bool GameState::PausePrevented() noexcept { return _pausePrevented; }
    void GameState::PausePrevented(bool value) noexcept { _pausePrevented = value; }
    bool GameState::MenuPause() noexcept { return _menuPause; }
    bool GameState::DialogPause() noexcept { return _dialogPause; }
    MatchState GameState::MatchState() noexcept { return _matchState; }
    void GameState::MatchState(MphRead::MatchState value) noexcept { _matchState = value; }
    TransitionState GameState::TransitionState() noexcept { return _transitionState; }
    void GameState::TransitionState(MphRead::TransitionState value) noexcept { _transitionState = value; }
    bool GameState::InRoomTransition() noexcept { return _transitionState != MphRead::TransitionState::None; }
    EscapeState GameState::EscapeState() noexcept { return _escapeState; }
    void GameState::EscapeState(MphRead::EscapeState value) noexcept { _escapeState = value; }
    float GameState::EscapeTimer() noexcept { return _escapeTimer; }
    void GameState::EscapeTimer(float value) noexcept { _escapeTimer = value; }
    bool GameState::EscapePaused() noexcept { return _escapePaused; }
    void GameState::EscapePaused(bool value) noexcept { _escapePaused = value; }

    GameState::IntSlots& GameState::EncounterState() noexcept { return _encounterState; }
    GameState::BoolRooms& GameState::CompletedRandomEncounterRooms() noexcept { return _completedRandomEncounterRooms; }
    std::int32_t GameState::TransitionRoomId() noexcept { return _transitionRoomId; }
    void GameState::TransitionRoomId(std::int32_t value) noexcept { _transitionRoomId = value; }
    bool GameState::TransitionAltForm() noexcept { return _transitionAltForm; }
    void GameState::TransitionAltForm(bool value) noexcept { _transitionAltForm = value; }
    std::int32_t GameState::ActivePlayers() noexcept { return _activePlayers; }
    void GameState::ActivePlayers(std::int32_t value) noexcept { _activePlayers = value; }
    GameState::NicknameSlots& GameState::Nicknames() noexcept { return _nicknames; }
    GameState::IntSlots& GameState::Stars() noexcept { return _stars; }
    GameState::IntSlots& GameState::Standings() noexcept { return _standings; }
    GameState::IntSlots& GameState::TeamStandings() noexcept { return _teamStandings; }
    GameState::IntSlots& GameState::ResultSlots() noexcept { return _resultSlots; }
    std::int32_t GameState::PrimeHunter() noexcept { return _primeHunter; }
    void GameState::PrimeHunter(std::int32_t value) noexcept { _primeHunter = value; }

    bool GameState::Teams() noexcept { return _teams; }
    void GameState::Teams(bool value) noexcept { _teams = value; }
    std::int32_t GameState::TeamCount() noexcept { return _teamCount; }
    void GameState::TeamCount(std::int32_t value) noexcept { _teamCount = value; }
    bool GameState::FriendlyFire() noexcept { return _friendlyFire; }
    void GameState::FriendlyFire(bool value) noexcept { _friendlyFire = value; }
    std::int32_t GameState::PointGoal() noexcept { return _pointGoal; }
    void GameState::PointGoal(std::int32_t value) noexcept { _pointGoal = value; }
    float GameState::TimeGoal() noexcept { return _timeGoal; }
    void GameState::TimeGoal(float value) noexcept { _timeGoal = value; }
    std::int32_t GameState::DamageLevel() noexcept { return 1; }
    void GameState::DamageLevel(std::int32_t value) noexcept { (void)value; }
    bool GameState::OctolithReset() noexcept { return _octolithReset; }
    void GameState::OctolithReset(bool value) noexcept { _octolithReset = value; }
    bool GameState::RadarPlayers() noexcept { return _radarPlayers; }
    void GameState::RadarPlayers(bool value) noexcept { _radarPlayers = value; }
    bool GameState::AffinityWeapons() noexcept { return _affinityWeapons; }
    void GameState::AffinityWeapons(bool value) noexcept { _affinityWeapons = value; }
    bool GameState::ShadowFreeze() noexcept { return _shadowFreeze; }
    void GameState::ShadowFreeze(bool value) noexcept { _shadowFreeze = value; }
    float GameState::MatchTime() noexcept { return _matchTime; }
    void GameState::MatchTime(float value) noexcept { _matchTime = value; }
    bool GameState::ForceEndGame() noexcept { return _forceEndGame; }
    void GameState::ForceEndGame(bool value) noexcept { _forceEndGame = value; }

    GameState::IntSlots& GameState::Points() noexcept { return _points; }
    GameState::IntSlots& GameState::TeamPoints() noexcept { return _teamPoints; }
    GameState::IntSlots& GameState::Kills() noexcept { return _kills; }
    GameState::IntSlots& GameState::TeamKills() noexcept { return _teamKills; }
    GameState::IntSlots& GameState::Deaths() noexcept { return _deaths; }
    GameState::IntSlots& GameState::TeamDeaths() noexcept { return _teamDeaths; }
    GameState::FloatSlots& GameState::Time() noexcept { return _time; }
    GameState::FloatSlots& GameState::TeamTime() noexcept { return _teamTime; }
    GameState::IntSlots& GameState::BeamDamageMax() noexcept { return _beamDamageMax; }
    GameState::IntSlots& GameState::BeamDamageDealt() noexcept { return _beamDamageDealt; }
    GameState::IntSlots& GameState::DamageCount() noexcept { return _damageCount; }
    GameState::IntSlots& GameState::AltDamageCount() noexcept { return _altDamageCount; }
    GameState::IntSlots& GameState::KillStreak() noexcept { return _killStreak; }
    GameState::IntSlots& GameState::Suicides() noexcept { return _suicides; }
    GameState::IntSlots& GameState::FriendlyKills() noexcept { return _friendlyKills; }
    GameState::IntSlots& GameState::HeadshotKills() noexcept { return _headshotKills; }
    GameState::BeamKillSlots& GameState::BeamKills() noexcept { return _beamKills; }
    GameState::IntSlots& GameState::OctolithScores() noexcept { return _octolithScores; }
    GameState::IntSlots& GameState::OctolithDrops() noexcept { return _octolithDrops; }
    GameState::IntSlots& GameState::OctolithStops() noexcept { return _octolithStops; }
    GameState::IntSlots& GameState::NodesCaptured() noexcept { return _nodesCaptured; }
    GameState::IntSlots& GameState::NodesLost() noexcept { return _nodesLost; }
    GameState::IntSlots& GameState::KillsAsPrime() noexcept { return _killsAsPrime; }
    GameState::IntSlots& GameState::PrimesKilled() noexcept { return _primesKilled; }
    const GameState::ModeStateAction& GameState::ModeState() noexcept { return _modeState; }

    bool GameState::QueuedOublietteUnlockMessage() noexcept { return _queuedOublietteUnlockMessage; }
    void GameState::QueuedOublietteUnlockMessage(bool value) noexcept { _queuedOublietteUnlockMessage = value; }
    std::int32_t GameState::QueuedOctolithMessageId() noexcept { return _queuedOctolithMessageId; }
    void GameState::QueuedOctolithMessageId(std::int32_t value) noexcept { _queuedOctolithMessageId = value; }

    StorySave::StorySave()
    {
        RoomState = std::make_shared<ManagedArray<ByteArray>>(66);
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(RoomState->Length()); ++i)
        {
            (*RoomState)[static_cast<std::size_t>(i)]
                = std::make_shared<ManagedArray<std::uint8_t>>(60);
        }

        EnemyEncounters = std::make_shared<ManagedArray<ByteArray>>(8);
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(EnemyEncounters->Length()); ++i)
        {
            (*EnemyEncounters)[static_cast<std::size_t>(i)]
                = std::make_shared<ManagedArray<std::uint8_t>>(8);
        }

        const Entities::PlayerValues& values = Metadata::PlayerValues.at(0);
        Health = HealthMax = UncheckedSubtract(values.EnergyTank, 1);
        ManagedAt(Ammo, 0) = ManagedAt(AmmoMax, 0) = 400;
        ManagedAt(Ammo, 1) = 0;
        ManagedAt(AmmoMax, 1) = 50;
        Weapons = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(WeaponUnlockBits::PowerBeam)
            | static_cast<std::uint16_t>(WeaponUnlockBits::Missile));

        if (Cheats::StartWithAllUpgrades())
        {
            Health = HealthMax = 799;
            ManagedAt(Ammo, 0) = ManagedAt(AmmoMax, 0) = 4000;
            ManagedAt(Ammo, 1) = 950;
            ManagedAt(AmmoMax, 1) = 950;
            Weapons = 0xFF;
        }

        ManagedAt(WeaponSlots, 0) = static_cast<std::int32_t>(BeamType::PowerBeam);
        ManagedAt(WeaponSlots, 1) = static_cast<std::int32_t>(BeamType::Missile);
        ManagedAt(WeaponSlots, 2) = static_cast<std::int32_t>(BeamType::None);

        UpdateLogbook(0);
        UpdateLogbook(1);
        UpdateLogbook(2);
        UpdateLogbook(3);
        UpdateLogbook(4);
        UpdateLogbook(5);
        UpdateLogbook(6);
        UpdateLogbook(26);
        UpdateLogbook(28);

        if (Cheats::StartWithAllOctoliths())
        {
            FoundOctoliths = CurrentOctoliths = 0xFF;
        }
    }

    std::int32_t StorySave::InitRoomState(
        std::int32_t roomId, std::int32_t entityId, bool active,
        std::int32_t activeState, std::int32_t inactiveState)
    {
        if (entityId == -1 || roomId < 27)
        {
            return 0;
        }
        if (roomId > 92 || entityId > 239)
        {
            return 1;
        }

        roomId = UncheckedSubtract(roomId, 27);
        activeState &= 3;
        inactiveState &= 3;
        const std::int32_t byteIndex = entityId / 4;
        std::int32_t pairIndex = entityId % 4;
        pairIndex = UncheckedMultiply(pairIndex, 2);
        const std::int32_t pairMask = ShiftLeftInt32(3, pairIndex);

        ByteArray& row = ManagedAt(RoomState, roomId);
        std::uint8_t& value = ManagedAt(row, byteIndex);
        if ((static_cast<std::int32_t>(value) & pairMask) == 0)
        {
            value = static_cast<std::uint8_t>(
                static_cast<std::int32_t>(value) & ~pairMask);
            const std::int32_t state = active ? activeState : inactiveState;
            value = static_cast<std::uint8_t>(
                static_cast<std::int32_t>(value)
                | ShiftLeftInt32(state, pairIndex));
        }
        return GetRoomState(UncheckedAdd(roomId, 27), entityId);
    }

    std::int32_t StorySave::GetRoomState(
        std::int32_t roomId, std::int32_t entityId) const
    {
        if (entityId == -1 || roomId < 27 || roomId > 92)
        {
            return 0;
        }
        if (entityId > 239)
        {
            return 1;
        }

        roomId = UncheckedSubtract(roomId, 27);
        const std::int32_t byteIndex = entityId / 4;
        std::int32_t pairIndex = entityId % 4;
        pairIndex = UncheckedMultiply(pairIndex, 2);
        const ByteArray& row = ManagedAt(RoomState, roomId);
        const std::uint8_t value = ManagedAt(row, byteIndex);
        return UncheckedSubtract(
            ShiftRight(
                static_cast<std::int32_t>(value), pairIndex) & 3,
            1);
    }

    void StorySave::SetRoomState(
        std::int32_t roomId, std::int32_t entityId, std::int32_t state)
    {
        if (entityId == -1 || roomId < 27 || roomId > 92 || entityId > 239)
        {
            return;
        }

        roomId = UncheckedSubtract(roomId, 27);
        state &= 3;
        const std::int32_t byteIndex = entityId / 4;
        std::int32_t pairIndex = entityId % 4;
        pairIndex = UncheckedMultiply(pairIndex, 2);
        const std::int32_t pairMask = ShiftLeftInt32(3, pairIndex);

        ByteArray& row = ManagedAt(RoomState, roomId);
        std::uint8_t& value = ManagedAt(row, byteIndex);
        value = static_cast<std::uint8_t>(
            static_cast<std::int32_t>(value) & ~pairMask);
        value = static_cast<std::uint8_t>(
            static_cast<std::int32_t>(value)
            | ShiftLeftInt32(state, pairIndex));
    }

    bool StorySave::CheckVisitedRoom(std::int32_t roomId) const
    {
        if (roomId < 27 || roomId > 92)
        {
            return false;
        }
        roomId = UncheckedSubtract(roomId, 27);
        const std::int32_t byteIndex = roomId / 8;
        const std::int32_t bitIndex = roomId % 8;
        return (static_cast<std::int32_t>(ManagedAt(VisitedRooms, byteIndex))
            & ShiftLeftInt32(1, bitIndex)) != 0;
    }

    void StorySave::SetVisitedRoom(std::int32_t roomId)
    {
        if (roomId < 27 || roomId > 92)
        {
            return;
        }
        roomId = UncheckedSubtract(roomId, 27);
        const std::int32_t byteIndex = roomId / 8;
        const std::int32_t bitIndex = roomId % 8;
        std::uint8_t& value = ManagedAt(VisitedRooms, byteIndex);
        value = static_cast<std::uint8_t>(
            static_cast<std::int32_t>(value) | ShiftLeftInt32(1, bitIndex));
    }

    bool StorySave::CheckVisitedConnector(
        std::int32_t connectorId, std::int32_t areaId) const
    {
        if (connectorId < 0 || connectorId > 63)
        {
            return false;
        }
        if (connectorId >= 32)
        {
            const std::int32_t index = UncheckedAdd(areaId & ~1, 1);
            const std::int32_t value = ManagedAt(VisitedConnectors, index);
            return (ShiftRight(value, UncheckedSubtract(connectorId, 32)) & 1) != 0;
        }
        const std::int32_t value = ManagedAt(VisitedConnectors, areaId & ~1);
        return (ShiftRight(value, connectorId) & 1) != 0;
    }

    void StorySave::SetVisitedConnector(
        std::int32_t connectorId, std::int32_t areaId)
    {
        if (connectorId < 0 || connectorId > 63)
        {
            return;
        }
        if (connectorId >= 32)
        {
            const std::int32_t index = UncheckedAdd(areaId & ~1, 1);
            std::int32_t& value = ManagedAt(VisitedConnectors, index);
            value = std::bit_cast<std::int32_t>(
                std::bit_cast<std::uint32_t>(value)
                | std::bit_cast<std::uint32_t>(
                    ShiftLeftInt32(1, UncheckedSubtract(connectorId, 32))));
        }
        else
        {
            std::int32_t& value = ManagedAt(VisitedConnectors, areaId & ~1);
            value = std::bit_cast<std::int32_t>(
                std::bit_cast<std::uint32_t>(value)
                | std::bit_cast<std::uint32_t>(ShiftLeftInt32(1, connectorId)));
        }
    }

    bool StorySave::CheckFoundOctolith(std::int32_t areaId) const
    {
        return (static_cast<std::uint32_t>(FoundOctoliths)
            & std::bit_cast<std::uint32_t>(ShiftLeftInt32(1, areaId))) != 0;
    }

    std::int32_t StorySave::CountFoundOctoliths() const
    {
        std::int32_t count = 0;
        for (std::int32_t i = 0; i < 8; ++i)
        {
            if (CheckFoundOctolith(i))
            {
                count = UncheckedAdd(count, 1);
            }
        }
        return count;
    }

    void StorySave::UpdateFoundOctolith(std::int32_t areaId)
    {
        const std::uint16_t bit = static_cast<std::uint16_t>(
            std::bit_cast<std::uint32_t>(ShiftLeftInt32(1, areaId)));
        FoundOctoliths = static_cast<std::uint16_t>(FoundOctoliths | bit);
        CurrentOctoliths = static_cast<std::uint16_t>(CurrentOctoliths | bit);
    }

    bool StorySave::CheckFoundArtifact(
        std::int32_t artifactId, std::int32_t modelId) const
    {
        const std::int32_t shift = UncheckedAdd(artifactId, UncheckedMultiply(3, modelId));
        return (Artifacts
            & std::bit_cast<std::uint32_t>(ShiftLeftInt32(1, shift))) != 0;
    }

    std::int32_t StorySave::CountFoundArtifacts(std::int32_t modelId) const
    {
        std::int32_t count = 0;
        for (std::int32_t i = 0; i < 3; ++i)
        {
            if (CheckFoundArtifact(i, modelId))
            {
                count = UncheckedAdd(count, 1);
            }
        }
        return count;
    }

    void StorySave::UpdateFoundArtifact(
        std::int32_t artifactId, std::int32_t modelId)
    {
        const std::int32_t shift = UncheckedAdd(artifactId, UncheckedMultiply(3, modelId));
        Artifacts |= std::bit_cast<std::uint32_t>(ShiftLeftInt32(1, shift));
    }

    std::int32_t StorySave::GetEnemyOctolithDrop(std::int32_t hunter) const
    {
        for (std::int32_t i = 0; i < 8; ++i)
        {
            const std::uint32_t shift
                = static_cast<std::uint32_t>(UncheckedMultiply(4, i)) & 31U;
            if (((LostOctoliths >> shift) & 15U)
                == static_cast<std::uint32_t>(hunter))
            {
                return i;
            }
        }
        return 8;
    }

    void StorySave::UpdateLogbook(std::int32_t scanId)
    {
        assert(scanId >= 0 && scanId < 68 * 8);
        const std::int32_t index = scanId / 8;
        const std::int32_t remainder = scanId % 8;
        const std::uint8_t bit = static_cast<std::uint8_t>(
            std::bit_cast<std::uint32_t>(ShiftLeftInt32(1, remainder)));

        std::uint8_t& value = ManagedAt(Logbook, index);
        if ((value & bit) == 0)
        {
            value = static_cast<std::uint8_t>(value | bit);
            const std::int32_t category = Text::Strings::GetScanEntryCategory(scanId);
            if (category < 3)
            {
                ScanCount = UncheckedAdd(ScanCount, 1);
            }
            else if (category == 3)
            {
                EquipmentCount = UncheckedAdd(EquipmentCount, 1);
            }
        }
    }

    bool StorySave::CheckLogbook(std::int32_t scanId) const
    {
        assert(scanId >= 0 && scanId < 68 * 8);
        const std::int32_t index = scanId / 8;
        const std::int32_t remainder = scanId % 8;
        const std::uint8_t bit = static_cast<std::uint8_t>(
            std::bit_cast<std::uint32_t>(ShiftLeftInt32(1, remainder)));
        return (ManagedAt(Logbook, index) & bit) != 0;
    }

    std::int32_t StorySave::GetLogbookCount(
        bool unlockedOnly, const std::shared_ptr<ManagedArray<char>>& categories) const
    {
        if (!categories)
        {
            throw System::ArgumentNullException("source");
        }
        const auto logbook = Text::Strings::ReadStringTable(Text::StringTables::ScanLog);
        RequireShared(logbook);

        std::int32_t result = 0;
        for (std::int32_t i = 0;
             i < static_cast<std::int32_t>(logbook->size()); ++i)
        {
            const auto& entry = (*logbook)[static_cast<std::size_t>(i)];
            const char category = static_cast<char>(entry->Category);
            bool categoryMatch = false;
            for (std::int32_t c = 0;
                 c < static_cast<std::int32_t>(categories->Length()); ++c)
            {
                if (ManagedAt(categories, c) == category)
                {
                    categoryMatch = true;
                    break;
                }
            }
            if (categoryMatch && (!unlockedOnly || CheckLogbook(i)))
            {
                result = UncheckedAdd(result, 1);
            }
        }
        return result;
    }

    std::int32_t StorySave::GetMaxScanCount() const
    {
        auto categories = std::make_shared<ManagedArray<char>>(3);
        ManagedAt(categories, 0) = 'L';
        ManagedAt(categories, 1) = 'B';
        ManagedAt(categories, 2) = 'O';
        return GetLogbookCount(false, categories);
    }

    std::int32_t StorySave::GetCompletionPercentage() const
    {
        const std::int32_t maxScans = GetMaxScanCount();
        if (maxScans == 0)
        {
            return 0;
        }

        std::int32_t count = ScanCount;
        for (std::int32_t i = 1; i < 8; ++i)
        {
            if (i == 2)
            {
                continue;
            }
            if ((static_cast<std::int32_t>(Weapons) & ShiftLeftInt32(1, i)) != 0)
            {
                count = UncheckedAdd(count, 1);
            }
        }
        for (std::int32_t i = 0; i < 8; ++i)
        {
            for (std::int32_t j = 0; j < 3; ++j)
            {
                const std::int32_t shift = UncheckedAdd(UncheckedMultiply(i, 3), j);
                if ((Artifacts
                    & std::bit_cast<std::uint32_t>(
                        ShiftLeftInt32(1, shift))) != 0)
                {
                    count = UncheckedAdd(count, 1);
                }
            }
        }
        for (std::int32_t i = 0; i < 8; ++i)
        {
            if ((static_cast<std::int32_t>(FoundOctoliths)
                & ShiftLeftInt32(1, i)) != 0)
            {
                count = UncheckedAdd(count, 1);
            }
        }

        const std::int32_t energyTank = Metadata::PlayerValues.at(0).EnergyTank;
        const std::int32_t etankCount = HealthMax / energyTank;
        const std::int32_t missileCount
            = UncheckedSubtract(ManagedAt(AmmoMax, 1), 50) / 100;
        const std::int32_t uaCount
            = UncheckedSubtract(ManagedAt(AmmoMax, 0), 400) / 300;
        count = UncheckedAdd(count, UncheckedAdd(etankCount, UncheckedAdd(missileCount, uaCount)));

        return UncheckedMultiply(100, count) / UncheckedAdd(maxScans, 66);
    }

    void StorySave::CopyTo(StorySave* other) const
    {
        const auto& sourceRooms = RequireShared(RoomState);
        for (std::int32_t i = 0;
             i < static_cast<std::int32_t>(sourceRooms->Length()); ++i)
        {
            const ByteArray source = ManagedAt(RoomState, i);
            StorySave* destinationSave = &RequireReference(other);
            const ByteArray destination = ManagedAt(destinationSave->RoomState, i);
            const std::size_t sourceLength = RequireShared(source)->Length();
            CopyManagedArray(source, destination, sourceLength, "destinationArray");
        }

        const auto& sourceEncounters = RequireShared(EnemyEncounters);
        for (std::int32_t i = 0;
             i < static_cast<std::int32_t>(sourceEncounters->Length()); ++i)
        {
            const ByteArray source = ManagedAt(EnemyEncounters, i);
            StorySave* destinationSave = &RequireReference(other);
            const ByteArray destination = ManagedAt(destinationSave->EnemyEncounters, i);
            const std::size_t sourceLength = RequireShared(source)->Length();
            CopyManagedArray(source, destination, sourceLength, "destinationArray");
        }

        StorySave* destinationSave = &RequireReference(other);
        CopyManagedArray(VisitedRooms, destinationSave->VisitedRooms,
            RequireShared(VisitedRooms)->Length(), "array");
        CopyManagedArray(VisitedConnectors, destinationSave->VisitedConnectors,
            RequireShared(VisitedConnectors)->Length(), "array");
        CopyManagedArray(TriggerState, destinationSave->TriggerState,
            RequireShared(TriggerState)->Length(), "array");
        CopyManagedArray(Logbook, destinationSave->Logbook,
            RequireShared(Logbook)->Length(), "array");

        destinationSave->ScanCount = ScanCount;
        destinationSave->EquipmentCount = EquipmentCount;
        destinationSave->CheckpointEntityId = CheckpointEntityId;
        destinationSave->CheckpointRoomId = CheckpointRoomId;
        destinationSave->Health = Health;
        destinationSave->HealthMax = HealthMax;

        CopyManagedArray(Ammo, destinationSave->Ammo, RequireShared(Ammo)->Length(), "array");
        CopyManagedArray(AmmoMax, destinationSave->AmmoMax,
            RequireShared(AmmoMax)->Length(), "array");
        CopyManagedArray(WeaponSlots, destinationSave->WeaponSlots,
            RequireShared(WeaponSlots)->Length(), "array");

        destinationSave->Weapons = Weapons;
        destinationSave->Artifacts = Artifacts;
        destinationSave->Artifacts = Artifacts;
        destinationSave->FoundOctoliths = FoundOctoliths;
        destinationSave->CurrentOctoliths = CurrentOctoliths;
        destinationSave->LostOctoliths = LostOctoliths;
        destinationSave->Areas = Areas;
        destinationSave->BossFlags = BossFlags;

        CopyManagedArray(AreaHunters, destinationSave->AreaHunters,
            RequireShared(AreaHunters)->Length(), "array");
        destinationSave->DefeatedHunters = DefeatedHunters;
    }

    StorySave::ByteArray GameState::ByteArrayConverter::Read(
        const std::shared_ptr<ManagedArray<std::int16_t>>& values)
    {
        if (!values)
        {
            return nullptr;
        }
        auto result = std::make_shared<ManagedArray<std::uint8_t>>(values->Length());
        for (std::int32_t i = 0;
             i < static_cast<std::int32_t>(values->Length()); ++i)
        {
            ManagedAt(result, i) = static_cast<std::uint8_t>(ManagedAt(values, i));
        }
        return result;
    }

    std::vector<std::uint8_t> GameState::ByteArrayConverter::Write(
        const StorySaveValue::ByteArray& values)
    {
        const auto& array = RequireShared(values);
        std::vector<std::uint8_t> result(array->Length());
        for (std::size_t i = 0; i < array->Length(); ++i)
        {
            result[i] = (*array)[i];
        }
        return result;
    }

    void GameState::PauseMenu()
    {
        _menuPause = true;
        SfxInstance()->StopAllSound();
        Sound::Sfx::TimedSfxMute
            = UncheckedAdd(Sound::Sfx::TimedSfxMute, 1);
    }

    void GameState::UnpauseMenu()
    {
        _menuPause = false;
        Sound::Sfx::TimedSfxMute
            = UncheckedSubtract(Sound::Sfx::TimedSfxMute, 1);
    }

    void GameState::PauseDialog() noexcept
    {
        _pausingDialog = true;
    }

    void GameState::UnpauseDialog() noexcept
    {
        _unpausingDialog = true;
    }

    void GameState::ApplyPause()
    {
        Formats::CameraSequence* current = Formats::CameraSequence::Current();
        if (current != nullptr && HasCamFlag(
            current->Flags(), Formats::CamSeqFlags::BlockInput))
        {
            return;
        }
        if (_pausingDialog)
        {
            _dialogPause = true;
        }
        if (_unpausingDialog)
        {
            _dialogPause = false;
        }
        _pausingDialog = false;
        _unpausingDialog = false;
    }

    bool GameState::IsTeamMode(GameMode mode)
    {
        return mode == GameMode::BattleTeams
            || mode == GameMode::SurvivalTeams
            || mode == GameMode::Capture
            || mode == GameMode::BountyTeams
            || mode == GameMode::NodesTeams
            || mode == GameMode::DefenderTeams;
    }

    void GameState::Setup(Scene* scene)
    {
        (void)RequireReference(scene);
        if (IsTeamMode(_mode))
        {
            _teams = true;
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(SlotCapacity); ++i)
            {
                auto player = PlayerAt(i);
                if (HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active))
                {
                    Mods::Multiplayer::TeamVisuals::Apply(*player);
                }
            }
        }

        _modeState = &GameState::ModeStateAdventure;
        if (_mode == GameMode::Battle
            || _mode == GameMode::BattleTeams)
        {
            _pointGoal = 7;
            _matchTime = static_cast<float>(7 * 60);
            _modeState = &GameState::ModeStateBattle;
        }
        else if (_mode == GameMode::Survival
            || _mode == GameMode::SurvivalTeams)
        {
            _pointGoal = 2;
            _matchTime = static_cast<float>(15 * 60);
            _modeState = &GameState::ModeStateSurvival;
        }
        else if (_mode == GameMode::Bounty
            || _mode == GameMode::BountyTeams)
        {
            _pointGoal = 3;
            _matchTime = static_cast<float>(15 * 60);
            _modeState = &GameState::ModeStateBounty;
        }
        else if (_mode == GameMode::Capture)
        {
            _pointGoal = 5;
            _matchTime = static_cast<float>(15 * 60);
            _modeState = &GameState::ModeStateCapture;
        }
        else if (_mode == GameMode::Defender
            || _mode == GameMode::DefenderTeams)
        {
            _timeGoal = 1.5F * 60.0F;
            _matchTime = static_cast<float>(15 * 60);
            _modeState = &GameState::ModeStateDefender;
        }
        else if (_mode == GameMode::Nodes
            || _mode == GameMode::NodesTeams)
        {
            _pointGoal = 70;
            _matchTime = static_cast<float>(15 * 60);
            _modeState = &GameState::ModeStateNodes;
        }
        else if (_mode == GameMode::PrimeHunter)
        {
            _timeGoal = 1.5F * 60.0F;
            _matchTime = static_cast<float>(15 * 60);
            _modeState = &GameState::ModeStatePrimeHunter;
        }

        if (Formats::CameraSequence::Intro() != nullptr)
        {
            RequireReference(Formats::CameraSequence::Intro()).Initialize();
            auto camera = RequireShared(MainPlayer()->CameraInfo());
            RequireReference(Formats::CameraSequence::Intro()).SetUp(*camera, 0);
            OrCamFlag(Formats::CameraSequence::Intro(), Formats::CamSeqFlags::Loop);
            scene->SetFade(FadeType::FadeInBlack, 20.0F / 30.0F, true);
        }

        _forceEndGame = false;
        _tempoChanged = false;
        _stateChanged = false;
        _lastAlarmTime = 0.0F;
        _nextAlarmIndex = 0;
    }

    void GameState::ResetMatchProgress() noexcept
    {
        _matchState = MphRead::MatchState::InProgress;
        _forceEndGame = false;
        _tempoChanged = false;
        _stateChanged = false;
        _matchEndTime = 0.0F;
        _lastAlarmTime = 0.0F;
        _nextAlarmIndex = 0;
    }

    void GameState::UpdateTime(Scene* scene)
    {
        (void)RequireReference(scene);
        if (_matchTime > 0.0F)
        {
            _matchTime = std::max(
                _matchTime - scene->FrameTime(), 0.0F);
        }
    }

    void GameState::ProcessFrame(Scene* scene)
    {
        (void)RequireReference(scene);

        Formats::CameraSequence* current = Formats::CameraSequence::Current();
        if (Multiplayer() && current != nullptr && current->IsIntro()
            && !Mods::Headless::Active())
        {
            assert(current->CamInfoRef() == MainPlayer()->CameraInfo().get());
            current->Process();
        }

        if (_matchState == MphRead::MatchState::InProgress)
        {
            if (SinglePlayer() && !_pausePrevented
                && !scene->MoviePlaying())
            {
                if (_menuPause
                    && MainPlayer()->Controls().Pause().IsPressed())
                {
                    SfxInstance()->PlayFreeSfx(SfxId::MENU_CANCEL);
                    UnpauseMenu();
                    MainPlayer()->EndMenuPauseHud();
                    MainPlayer()->Controls().Pause().SetIsPressed(false);
                    return;
                }

                current = Formats::CameraSequence::Current();
                if (!_menuPause
                    && !(current != nullptr && current->BlockInput())
                    && MainPlayer()->Controls().Pause().IsPressed())
                {
                    MainPlayer()->Controls().Pause().SetIsPressed(false);
                    PauseMenu();
                    MainPlayer()->SetUpMenuPauseHud();
                    SfxInstance()->PlayFreeSfx(SfxId::MENU_CONFIRM);
                }

                if (_menuPause)
                {
                    MainPlayer()->ProcessPauseMenu();
                    return;
                }
            }

            const MessageQueueReadOnly& queue = scene->MessageQueue();
            for (std::int32_t i = 0; i < queue.Count(); ++i)
            {
                const MessageInfo message = queue[i];
                if (message.Message == Message::Complete
                    && message.ExecuteFrame == scene->FrameCount())
                {
                    _matchTime = 0.0F;
                }
            }

            if (Multiplayer() && !Features::AllowInvalidTeams())
            {
                bool invalid = Entities::PlayerEntity::MaxPlayers() < 2;
                if (!invalid && _teams)
                {
                    std::array<bool, 4> teams{};
                    for (std::int32_t i = 0;
                         i < static_cast<std::int32_t>(SlotCapacity); ++i)
                    {
                        auto player = PlayerAt(i);
                        if (HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active))
                        {
                            if (static_cast<std::uint32_t>(player->TeamIndex())
                                < static_cast<std::uint32_t>(_teamCount))
                            {
                                ManagedAt(teams, player->TeamIndex()) = true;
                            }
                        }
                    }
                    std::int32_t representedTeams = 0;
                    for (std::int32_t team = 0; team < _teamCount; ++team)
                    {
                        if (ManagedAt(teams, team))
                        {
                            representedTeams = UncheckedAdd(representedTeams, 1);
                        }
                    }
                    invalid = representedTeams < 2;
                }
                if (invalid && !_menuPause)
                {
                    _matchTime = 0.0F;
                    current = Formats::CameraSequence::Current();
                    if (current != nullptr)
                    {
                        current->End();
                    }
                }
            }

            _modeState(scene);

            if (SinglePlayer() && _escapeTimer != -1.0F)
            {
                current = Formats::CameraSequence::Current();
                if (!_escapePaused && !_menuPause && !_dialogPause
                    && scene->FadeType() == FadeType::None
                    && !(current != nullptr
                        && HasCamFlag(current->Flags(), Formats::CamSeqFlags::BlockInput)))
                {
                    _escapeTimer -= scene->FrameTime();
                    if (_escapeState == MphRead::EscapeState::Escape)
                    {
                        Music::UpdateEscapeMusic();
                    }
                    else
                    {
                        UpdateEventSounds(_escapeTimer);
                    }
                }
                if (_escapeTimer <= 0.0F)
                {
                    if (_escapeState == MphRead::EscapeState::Escape
                        && MainPlayer()->Health() > 0)
                    {
                        scene->SendMessage(Message::Death, nullptr, MainPlayer(),
                            BoxInt32(0), BoxInt32(0));
                    }
                    _escapeTimer = -1.0F;
                }
            }

            if (_matchTime != 0.0F && !_forceEndGame)
            {
                if (Multiplayer() && _matchTime > 0.0F)
                {
                    const TimeSpanParts time = TimeSpanFromSeconds(_matchTime);
                    if (time.TotalMinutes < 1.0 && time.Seconds <= 59
                        && !_tempoChanged)
                    {
                        Music::UpdateTempo(307, 900.0F / 30.0F);
                        _tempoChanged = true;
                    }
                    if (time.TotalMinutes < 1.0 && time.Seconds <= 9)
                    {
                        float comparison = 1.0F;
                        if (time.Seconds <= 5)
                        {
                            if (Features::HalfSecondAlarm())
                            {
                                comparison = 0.5F;
                            }
                            else
                            {
                                comparison = ManagedAt(
                                    _alarmIntervals, _nextAlarmIndex);
                            }
                        }

                        if (_lastAlarmTime == 0.0F
                            || scene->ElapsedTime()
                                - _lastAlarmTime >= comparison)
                        {
                            SfxInstance()->PlaySample(
                                static_cast<std::int32_t>(SfxId::ALARM),
                                nullptr, false, false, -1.0F, false, false);
                            _lastAlarmTime = scene->ElapsedTime();
                            _nextAlarmIndex = UncheckedAdd(_nextAlarmIndex, 1);
                            if (_nextAlarmIndex
                                >= static_cast<std::int32_t>(_alarmIntervals.size()))
                            {
                                _nextAlarmIndex = 0;
                            }
                        }
                    }
                }
            }
            else
            {
                MainPlayer()->HudEndDisrupted();

                if ((_mode == GameMode::Survival
                    || _mode == GameMode::SurvivalTeams)
                    && !_forceEndGame)
                {
                    for (std::int32_t i = 0;
                         i < static_cast<std::int32_t>(SlotCapacity); ++i)
                    {
                        auto player = PlayerAt(i);
                        if (HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active)
                            && (player->Health() > 0
                                || ManagedAt(_teamDeaths, player->TeamIndex())
                                    <= _pointGoal))
                        {
                            ManagedAt(_time, i) = -1.0F;
                            ManagedAt(_teamTime, player->TeamIndex()) = -1.0F;
                        }
                    }
                    UpdateState();
                }

                _matchState = MphRead::MatchState::GameOver;
                _matchTime = 90.0F / 30.0F;
                scene->SetFade(FadeType::None, 0.0F, true);
                _stateChanged = true;
                _matchEndTime = scene->GlobalElapsedTime();
                SfxInstance()->StopFreeSfxScripts();
                SfxInstance()->StopAllSound();
                MainPlayer()->StopLongSfx();
                if (!SinglePlayer())
                {
                    Music::PlaySeq(SeqId::TIMEOUT);
                }
            }
        }
        else if (_matchState == MphRead::MatchState::GameOver)
        {
            auto winner = PlayerAt(ManagedAt(_resultSlots, 0));
            if (!IsResultTie() && winner->Health() > 0
                && HasLoadFlag(winner->LoadFlags(), Entities::LoadFlags::Active)
                && HasLoadFlag(winner->LoadFlags(), Entities::LoadFlags::Spawned))
            {
                if (_stateChanged)
                {
                    _stateChanged = false;
                    winner->SetUpMatchEndCamera();
                }
                MainPlayer()->UpdateMatchEndCamera(
                    winner,
                    scene->GlobalElapsedTime() - _matchEndTime);
            }
            else
            {
                EnsureIntroCamSeq();
            }

            if (_matchTime == 0.0F)
            {
                _matchState = MphRead::MatchState::Ending;
                _matchTime = MatchEndingSeconds;
            }
        }
        else if (_matchState == MphRead::MatchState::Ending)
        {
            EnsureIntroCamSeq();
            if (_matchTime == 0.0F)
            {
                _matchTime = -1.0F;
                if (Mods::Network::NetMatchEnd::ShouldLeaveAfterMatch())
                {
                    bool playPickedMap = false;
#if defined(MPHREAD_SHELL)
                    if (Mods::Launcher::Gui::Shell::CanPlayAnother())
                    {
                        const std::string room = Mods::MapPick::Chosen();
                        if (!room.empty())
                        {
                            Mods::Launcher::Gui::Shell::PlayAnother(room);
                            playPickedMap = true;
                        }
                    }
#endif
                    if (!playPickedMap)
                    {
                        scene->SetFade(FadeType::FadeOutBlack,
                            20.0F / 30.0F, true, AfterFade::Exit);
                    }
                }
            }
        }
    }

    void GameState::EnsureIntroCamSeq()
    {
        if (Multiplayer()
            && Formats::CameraSequence::Current() == nullptr
            && Formats::CameraSequence::Intro() != nullptr)
        {
            RequireReference(Formats::CameraSequence::Intro()).SetUp(
                *RequireShared(MainPlayer()->CameraInfo()), 0);
            RequireShared(MainPlayer()->CameraInfo())->Update();
            OrCamFlag(Formats::CameraSequence::Intro(), Formats::CamSeqFlags::Loop);
        }
    }

    AreaState GameState::GetAreaState(std::int32_t areaId, StorySaveValue* save)
    {
        if (save == nullptr)
        {
            save = StorySave.get();
        }
        if (save == nullptr)
        {
            return AreaState::None;
        }
        const std::int32_t shift = UncheckedMultiply(2, areaId);
        const std::int32_t value = ShiftRight(
            std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(save->BossFlags)),
            shift) & 3;
        return static_cast<AreaState>(value);
    }

    void GameState::ModeStateAdventure(Scene* scene)
    {
        (void)RequireReference(scene);
        MainPlayer()->SaveStatus();

        StorySaveValue* save = &RequireReference(StorySave.get());
        if ((save->Areas & 0x100) == 0)
        {
            if (_queuedOublietteUnlockMessage
                && scene->FadeType() == FadeType::FadeInBlack
                && !scene->MoviePlaying())
            {
                save->Areas = static_cast<std::uint16_t>(save->Areas | 0x100);
                save->CurrentOctoliths = 0;
                MainPlayer()->ShowDialog(Entities::DialogType::Okay, 43);
                _queuedOublietteUnlockMessage = false;
            }

            const MessageQueueReadOnly& queue = scene->MessageQueue();
            for (std::int32_t i = 0; i < queue.Count(); ++i)
            {
                const MessageInfo message = queue[i];
                if (message.Message == Message::UnlockOubliette
                    && message.ExecuteFrame == scene->FrameCount())
                {
                    if (save->CurrentOctoliths == 0xFF)
                    {
                        _pausePrevented = true;
                        scene->StartMovie(Movie::OublietteUnlock,
                            FadeType::FadeOutInWhite, 20.0F / 30.0F,
                            FadeType::FadeOutInBlack, 5.0F / 30.0F);
                        _queuedOublietteUnlockMessage = true;
                    }
                    else
                    {
                        auto enumerator = scene->GetCamSeqEntities().GetEnumerator();
                        while (enumerator.MoveNext())
                        {
                            const auto entity = enumerator.Current();
                            scene->SendMessage(Message::Activate, nullptr,
                                RequireShared(entity).get(), BoxInt32(0), BoxInt32(0));
                            break;
                        }
                    }
                    break;
                }
            }
        }

        if (MainPlayer()->Health() > 0)
        {
            const MessageQueueReadOnly& queue = scene->MessageQueue();
            for (std::int32_t i = 0; i < queue.Count(); ++i)
            {
                const MessageInfo message = queue[i];
                if (message.Message == Message::Checkpoint
                    && message.ExecuteFrame == scene->FrameCount())
                {
                    assert((scene->Room() != nullptr));
                    scene->SendMessage(Message::SetActive, nullptr,
                        message.Sender, BoxInt32(0), BoxInt32(0));
                    save->CheckpointEntityId = RequireReference(message.Sender).Id;
                    save->CheckpointRoomId = scene->RoomId();
                    UpdateCleanSave(false);
                    break;
                }
            }

            for (std::int32_t i = 0; i < queue.Count(); ++i)
            {
                const MessageInfo message = queue[i];
                if (message.Message == Message::LoadOubliette
                    && message.ExecuteFrame == scene->FrameCount())
                {
                    _transitionRoomId = 91;
                    scene->StartMovie(Movie::Gorea1Intro,
                        FadeType::FadeOutInWhite, 10.0F / 30.0F,
                        FadeType::FadeOutInWhite, 10.0F / 30.0F);
                    break;
                }
            }
        }
    }

    void GameState::EndIfPointGoalReached()
    {
        if (_pointGoal <= 0 || !Mods::Network::NetMatchEnd::MayEndOnScore())
        {
            return;
        }

        for (std::int32_t i = 0; i < static_cast<std::int32_t>(SlotCapacity); ++i)
        {
            auto player = PlayerAt(i);
            if (HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active)
                && ManagedAt(_teamPoints, player->TeamIndex()) >= _pointGoal)
            {
                ManagedAt(_teamPoints, player->TeamIndex()) = _pointGoal;
                _matchTime = 0.0F;
                break;
            }
        }
    }

    void GameState::ModeStateBattle(Scene* scene)
    {
        (void)scene;
        EndIfPointGoalReached();
    }

    void GameState::ModeStateSurvival(Scene* scene)
    {
        (void)RequireReference(scene);
        UpdateSurvival(scene->FrameTime());
    }

    void GameState::UpdateSurvival(float frameTime)
    {
        _radarPlayers = false;
        std::int32_t playersAlive = 0;
        std::int32_t botsAlive = 0;
        std::array<bool, 4> teamsAlive{};
        std::int32_t aliveTeamCount = 0;

        for (std::int32_t i = 0; i < static_cast<std::int32_t>(SlotCapacity); ++i)
        {
            auto player = PlayerAt(i);
            if (HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active)
                && (player->Health() > 0
                    || ManagedAt(_teamDeaths, player->TeamIndex()) <= _pointGoal))
            {
                ManagedAt(_time, i) += frameTime;
                if (player->IsBot())
                {
                    botsAlive = UncheckedAdd(botsAlive, 1);
                }
                else
                {
                    playersAlive = UncheckedAdd(playersAlive, 1);
                }
                if (_teams)
                {
                    if (static_cast<std::uint32_t>(player->TeamIndex())
                            < static_cast<std::uint32_t>(_teamCount)
                        && !ManagedAt(teamsAlive, player->TeamIndex()))
                    {
                        ManagedAt(teamsAlive, player->TeamIndex()) = true;
                        aliveTeamCount = UncheckedAdd(aliveTeamCount, 1);
                    }
                }
            }
        }

        if (Mods::Network::NetMatchEnd::MayEndOnScore()
            && (UncheckedAdd(playersAlive, botsAlive) < 2
                || (_teams && aliveTeamCount < 2)))
        {
            _matchTime = 0.0F;
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(SlotCapacity); ++i)
            {
                auto player = PlayerAt(i);
                if (HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active)
                    && (player->Health() > 0
                        || ManagedAt(_teamDeaths, player->TeamIndex()) <= _pointGoal))
                {
                    ManagedAt(_time, i) = -1.0F;
                }
            }
        }
        else if (UncheckedAdd(playersAlive, botsAlive) == 2
            && Entities::PlayerEntity::PlayerCount() > 2)
        {
            _radarPlayers = true;
        }
    }

    void GameState::ModeStateCapture(Scene* scene)
    {
        (void)scene;
        EndIfPointGoalReached();
    }

    void GameState::ModeStateBounty(Scene* scene)
    {
        (void)scene;
        EndIfPointGoalReached();
    }

    void GameState::ModeStateDefender(Scene* scene)
    {
        (void)scene;
        if (!Mods::Network::NetMatchEnd::MayEndOnScore())
        {
            return;
        }
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(SlotCapacity); ++i)
        {
            auto player = PlayerAt(i);
            if (HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active)
                && ManagedAt(_teamTime, player->TeamIndex()) >= _timeGoal)
            {
                _matchTime = 0.0F;
                break;
            }
        }
    }

    void GameState::ModeStateNodes(Scene* scene)
    {
        (void)scene;
        EndIfPointGoalReached();
    }

    void GameState::ModeStatePrimeHunter(Scene* scene)
    {
        (void)RequireReference(scene);
        if (_primeHunter == -1)
        {
            return;
        }

        auto player = PlayerAt(_primeHunter);
        if (!HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active))
        {
            _primeHunter = -1;
            return;
        }

        if (scene->FrameCount() % (10 * 2) == 0)
        {
            player->TakeDamage(1, Entities::DamageFlags::NoDmgInvuln,
                std::nullopt, nullptr);
        }
        if (_primeHunter != -1)
        {
            ManagedAt(_time, _primeHunter) += scene->FrameTime();
            if (ManagedAt(_time, _primeHunter) >= _timeGoal)
            {
                _matchTime = 0.0F;
            }
        }
    }

    void GameState::UpdateFrame(Scene* scene)
    {
        (void)RequireReference(scene);
        const Entities::PromptType prompt = MainPlayer()->DialogPromptType();
        const Entities::ConfirmState confirm = MainPlayer()->DialogConfirmState();

        const auto quit = [scene]()
        {
            scene->SetFade(FadeType::FadeOutBlack,
                10.0F / 30.0F, true, AfterFade::Exit);
            Music::Stop(10.0F / 30.0F);
            SfxInstance()->PlaySample(
                static_cast<std::int32_t>(SfxId::QUIT_GAME),
                nullptr, false, false, -1.0F, false, false);
        };

        if (prompt != Entities::PromptType::Any
            && confirm != Entities::ConfirmState::Okay)
        {
            SfxInstance()->StopFreeSfxScripts();
            if (confirm == Entities::ConfirmState::Yes)
            {
                if (prompt == Entities::PromptType::ShipHatch)
                {
                    EnterShip();
                    assert((scene->Room() != nullptr));
                    const std::int32_t roomId = scene->RoomId();
                    Movie movieId = Movie::None;
                    if (roomId == 27)
                    {
                        movieId = Movie::AlinosTakeoff;
                    }
                    else if (roomId == 45)
                    {
                        movieId = Movie::CATakeoff;
                    }
                    else if (roomId == 65)
                    {
                        movieId = Movie::VDOTakeoff;
                    }
                    else if (roomId == 77)
                    {
                        movieId = Movie::ArcterraTakeoff;
                    }

                    if (movieId != Movie::None
                        && !Cheats::SkipPlanetIntros())
                    {
                        scene->StartMovie(movieId,
                            FadeType::FadeOutInWhite, 20.0F / 30.0F,
                            FadeType::FadeOutBlack, 5.0F / 30.0F,
                            AfterMovie::EndGame);
                    }
                    else
                    {
                        scene->SetFade(FadeType::FadeOutWhite, 20.0F / 30.0F,
                            true, AfterFade::EnterShip);
                    }
                    Music::Stop(20.0F / 30.0F);
                    _pausePrevented = true;
                    SfxInstance()->PlaySample(
                        static_cast<std::int32_t>(SfxId::RETURN_TO_SHIP_YES),
                        nullptr, false, false, -1.0F, false, false);
                }
                else if (prompt == Entities::PromptType::GameOver)
                {
                    RestoreCleanSave();
                    StorySaveValue* save = &RequireReference(StorySave.get());
                    assert((scene->Room() != nullptr));

                    if (Cheats::ContinueFromCurrentRoom())
                    {
                        if (save->CheckpointRoomId != scene->RoomId())
                        {
                            save->CheckpointEntityId = -1;
                        }
                        save->CheckpointRoomId = scene->RoomId();
                    }
                    else if (save->CheckpointRoomId == -1)
                    {
                        save->CheckpointEntityId = -1;
                        const std::int32_t areaId = scene->AreaId();
                        if (areaId == 0 || areaId == 1)
                        {
                            save->CheckpointRoomId = 27;
                        }
                        else if (areaId == 2 || areaId == 3)
                        {
                            save->CheckpointRoomId = 45;
                        }
                        else if (areaId == 4 || areaId == 5)
                        {
                            save->CheckpointRoomId = 65;
                        }
                        else if (areaId == 6 || areaId == 7)
                        {
                            save->CheckpointRoomId = 77;
                        }
                        else if (areaId == 8)
                        {
                            save->CheckpointRoomId = 89;
                        }
                    }

                    if (save->CheckpointRoomId == -1)
                    {
                        quit();
                    }
                    _transitionRoomId = save->CheckpointRoomId;
                    SfxInstance()->PlaySample(
                        static_cast<std::int32_t>(SfxId::MENU_CONFIRM),
                        nullptr, false, false, -1.0F, false, false);
                    _pausePrevented = true;
                    scene->SetFade(FadeType::FadeOutWhite, 10.0F / 30.0F,
                        true, AfterFade::LoadRoom);
                    UnpauseDialog();
                    MainPlayer()->RestartLongSfx(true);
                }
            }
            else if (prompt == Entities::PromptType::GameOver)
            {
                quit();
            }
            else
            {
                MainPlayer()->RestartLongSfx();
                SfxInstance()->PlaySample(
                    static_cast<std::int32_t>(SfxId::RETURN_TO_SHIP_NO),
                    nullptr, false, false, -1.0F, false, false);
                UnpauseDialog();
            }

            MainPlayer()->DialogPromptType(Entities::PromptType::Any);
            MainPlayer()->DialogConfirmState(Entities::ConfirmState::Okay);
        }

        if (!_dialogPause)
        {
            const MessageQueueReadOnly& queue = scene->MessageQueue();
            if (MainPlayer()->Health() > 0)
            {
                for (std::int32_t i = 0; i < queue.Count(); ++i)
                {
                    const MessageInfo message = queue[i];
                    if (message.Message == Message::ShipHatch
                        && message.ExecuteFrame == scene->FrameCount())
                    {
                        assert((scene->Room() != nullptr));
                        ResetEscapeState(false);
                        MainPlayer()->DialogPromptType(Entities::PromptType::ShipHatch);
                        StorySaveValue* save = &RequireReference(StorySave.get());
                        save->CheckpointEntityId = RequireReference(message.Sender).Id;
                        save->CheckpointRoomId = scene->RoomId();
                        UpdateCleanSave(true);
                        MainPlayer()->ShowDialog(Entities::DialogType::YesNo, 1);
                        SfxInstance()->StopFreeSfxScripts();
                        SfxInstance()->PlayScript(
                            static_cast<std::int32_t>(SfxId::RETURN_TO_SHIP_SCR),
                            nullptr, false, -1.0F, false, false);
                        break;
                    }
                }

                for (std::int32_t i = 0; i < queue.Count(); ++i)
                {
                    const MessageInfo message = queue[i];
                    if (message.Message == Message::EscapeUpdate1
                        && message.ExecuteFrame == scene->FrameCount())
                    {
                        UpdateEscapeState(
                            UncheckedMultiply(UnboxInt32(message.Param1), 30),
                            UnboxInt32(message.Param2));
                    }
                }

                for (std::int32_t i = 0; i < queue.Count(); ++i)
                {
                    const MessageInfo message = queue[i];
                    if (message.Message == Message::EscapeUpdate2
                        && message.ExecuteFrame == scene->FrameCount())
                    {
                        UpdateEscapeState(
                            UnboxInt32(message.Param1), UnboxInt32(message.Param2));
                    }
                }

                for (std::int32_t i = 0; i < queue.Count(); ++i)
                {
                    const MessageInfo message = queue[i];
                    if (message.Message == Message::ShowPrompt
                        && message.ExecuteFrame == scene->FrameCount())
                    {
                        const std::int32_t promptType = UnboxInt32(message.Param2);
                        if (promptType == 0)
                        {
                            MainPlayer()->ShowDialog(
                                Entities::DialogType::Okay, UnboxInt32(message.Param1));
                        }
                        else if (promptType == 1)
                        {
                            MainPlayer()->ShowDialog(
                                Entities::DialogType::YesNo, UnboxInt32(message.Param1));
                        }
                    }
                }
            }

            for (std::int32_t i = 0; i < queue.Count(); ++i)
            {
                const MessageInfo message = queue[i];
                if (message.Message == Message::ShowWarning
                    && message.ExecuteFrame == scene->FrameCount())
                {
                    const std::int32_t messageId = UnboxInt32(message.Param1);
                    std::int32_t duration = UnboxInt32(message.Param2);
                    if (duration == 0)
                    {
                        duration = 15;
                    }
                    MainPlayer()->ShowDialog(
                        Entities::DialogType::Overlay, messageId, duration, 1);
                }
            }

            for (std::int32_t i = 0; i < queue.Count(); ++i)
            {
                const MessageInfo message = queue[i];
                if (message.Message == Message::ShowOverlay
                    && message.ExecuteFrame == scene->FrameCount())
                {
                    const std::int32_t messageId = UnboxInt32(message.Param1);
                    const std::int32_t duration = UnboxInt32(message.Param2);
                    MainPlayer()->ShowDialog(
                        Entities::DialogType::Overlay, messageId, duration, 0);
                }
            }
        }

        if (_queuedOctolithMessageId != -1
            && scene->FadeType() == FadeType::FadeInWhite
            && !scene->MoviePlaying())
        {
            MainPlayer()->ShowDialog(Entities::DialogType::Event, 7,
                static_cast<std::int32_t>(Entities::EventType::Octolith));
            scene->SendMessage(Message::ShowPrompt, MainPlayer(), nullptr,
                BoxInt32(_queuedOctolithMessageId), BoxInt32(0), 1);
            _queuedOctolithMessageId = -1;
        }

        const float countdown = MainPlayer()->DeathCountdown();
        if (SinglePlayer() && MainPlayer()->Health() == 0 && countdown > 0.0F)
        {
            if (countdown >= 145.0F / 30.0F)
            {
                _whiteoutStarted = false;
                _gameOverShown = false;
                if (_escapeState == MphRead::EscapeState::Escape)
                {
                    MainPlayer()->ShowDialog(
                        Entities::DialogType::Hud, 120, 69, 1);
                }
                else
                {
                    MainPlayer()->ShowDialog(
                        Entities::DialogType::Hud, 116, 45, 1);
                }
            }
            else if (countdown <= 1.0F / 30.0F && !_gameOverShown)
            {
                RequireShared(MainPlayer()->CameraInfo())->SetShake(0.0F);
                MainPlayer()->DialogPromptType(Entities::PromptType::GameOver);
                MainPlayer()->ShowDialog(Entities::DialogType::YesNo, 2);
                ResetEscapeState(true);
                _gameOverShown = true;
            }
            else if (countdown <= 50.0F / 30.0F && !_whiteoutStarted)
            {
                MainPlayer()->BeginWhiteout();
                _whiteoutStarted = true;
            }
        }
    }

    void GameState::UpdateBossFlags(std::int32_t areaId)
    {
        StorySaveValue* save = &RequireReference(StorySave.get());
        std::uint32_t flags = static_cast<std::uint32_t>(save->BossFlags);
        const std::int32_t shift = UncheckedMultiply(2, areaId);
        flags &= static_cast<std::uint32_t>(
            ~ShiftLeftInt32(3, shift));
        flags |= std::bit_cast<std::uint32_t>(
            ShiftLeftInt32(1, shift));
        save->BossFlags = static_cast<BossFlags>(flags);
    }

    void GameState::EnterShip()
    {
        StorySaveValue* save = &RequireReference(StorySave.get());
        std::uint8_t& triggerState = ManagedAt(save->TriggerState, 2);
        triggerState = static_cast<std::uint8_t>(triggerState & 0x7F);

        ReplaceBossFlag(save->BossFlags,
            BossFlags::Unit1B1Kill,
            BossFlags::Unit1B1Done);
        ReplaceBossFlag(save->BossFlags,
            BossFlags::Unit1B2Kill,
            BossFlags::Unit1B2Done);
        ReplaceBossFlag(save->BossFlags,
            BossFlags::Unit2B1Kill,
            BossFlags::Unit2B1Done);
        ReplaceBossFlag(save->BossFlags,
            BossFlags::Unit2B2Kill,
            BossFlags::Unit2B2Done);
        ReplaceBossFlag(save->BossFlags,
            BossFlags::Unit3B1Kill,
            BossFlags::Unit3B1Done);
        ReplaceBossFlag(save->BossFlags,
            BossFlags::Unit3B2Kill,
            BossFlags::Unit3B2Done);
        ReplaceBossFlag(save->BossFlags,
            BossFlags::Unit4B1Kill,
            BossFlags::Unit4B1Done);
        ReplaceBossFlag(save->BossFlags,
            BossFlags::Unit4B2Kill,
            BossFlags::Unit4B2Done);
    }

    void GameState::ResetEscapeState(bool updateSounds)
    {
        _escapeState = MphRead::EscapeState::None;
        _escapeTimer = -1.0F;
        _escapePaused = false;
        if (updateSounds)
        {
            UpdateEventSounds(-1.0F);
        }
    }

    void GameState::UpdateEscapeState(
        std::int32_t frames, std::int32_t stateId)
    {
        const auto state = static_cast<MphRead::EscapeState>(stateId);
        const float time = static_cast<float>(frames) / 30.0F;
        if (state == MphRead::EscapeState::None)
        {
            _escapeTimer = -1.0F;
            UpdateEventSounds(-1.0F);
        }
        else if (time == 0.0F)
        {
            _escapePaused = !_escapePaused;
        }
        else if (_escapeState != state || _escapeTimer == -1.0F)
        {
            _escapeTimer = time;
            _escapePaused = false;
            if (state == MphRead::EscapeState::Escape)
            {
                Sound::Sfx::QueueStream(VoiceId::VOICE_EVACUATE);
                Sound::Sfx::QueueStream(VoiceId::VOICE_EVACUATE, 3.0F);
                Sound::Sfx::QueueStream(VoiceId::VOICE_EVACUATE, 6.0F);
                Music::PlayMusic(MusicId::SEQ_OREGANO_M55);
                Music::UpdateTempo(245, 0.0F);
                StorySaveValue* save = &RequireReference(StorySave.get());
                std::uint8_t& triggerState = ManagedAt(save->TriggerState, 2);
                triggerState = static_cast<std::uint8_t>(triggerState | 0x80);
            }
            else
            {
                UpdateEventSounds(-1.0F);
            }
        }
        _escapeState = state;
    }

    void GameState::UpdateEventSounds(float timer)
    {
        Music::UpdateEventMusic(timer);
        if (timer > 165.0F / 30.0F)
        {
            _playedTimedEventSfx = false;
        }
        else if (timer >= 0.0F && !_playedTimedEventSfx)
        {
            MainPlayer()->PlayTimedSfx(SfxId::PUZZLE_TIMER1_SCR);
            _playedTimedEventSfx = true;
        }
        else if (timer < 0.0F)
        {
            MainPlayer()->StopTimedSfx(SfxId::PUZZLE_TIMER1_SCR);
            _playedTimedEventSfx = false;
        }
    }

    void GameState::UpdateState()
    {
        if (Entities::PlayerEntity::PlayerCount() == 0)
        {
            return;
        }

        IntSlots prevTeamPoints{};
        IntSlots prevTeamDeaths{};
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(SlotCapacity); ++i)
        {
            ManagedAt(prevTeamPoints, i) = ManagedAt(_teamPoints, i);
            ManagedAt(prevTeamDeaths, i) = ManagedAt(_teamDeaths, i);
            ManagedAt(_teamPoints, i) = 0;
            ManagedAt(_teamDeaths, i) = 0;
            ManagedAt(_teamKills, i) = 0;
            if (_mode == GameMode::Survival
                || _mode == GameMode::SurvivalTeams)
            {
                ManagedAt(_teamTime, i) = 0.0F;
            }
        }

        for (std::int32_t i = 0; i < static_cast<std::int32_t>(SlotCapacity); ++i)
        {
            auto player = PlayerAt(i);
            if (!HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Initial)
                || static_cast<std::uint32_t>(player->TeamIndex())
                    >= static_cast<std::uint32_t>(
                        _teams ? _teamCount : static_cast<std::int32_t>(SlotCapacity)))
            {
                continue;
            }

            std::int32_t& teamPoints = ManagedAt(_teamPoints, player->TeamIndex());
            teamPoints = UncheckedAdd(teamPoints, ManagedAt(_points, i));
            std::int32_t& teamDeaths = ManagedAt(_teamDeaths, player->TeamIndex());
            teamDeaths = UncheckedAdd(teamDeaths, ManagedAt(_deaths, i));
            std::int32_t& teamKills = ManagedAt(_teamKills, player->TeamIndex());
            teamKills = UncheckedAdd(teamKills, ManagedAt(_kills, i));

            if (_mode == GameMode::Survival
                || _mode == GameMode::SurvivalTeams)
            {
                if (ManagedAt(_time, i) == -1.0F
                    || (ManagedAt(_teamTime, player->TeamIndex()) != -1.0F
                        && ManagedAt(_teamTime, player->TeamIndex()) < ManagedAt(_time, i)))
                {
                    ManagedAt(_teamTime, player->TeamIndex()) = ManagedAt(_time, i);
                }
            }
            else if (_mode == GameMode::Defender
                || _mode == GameMode::DefenderTeams)
            {
                ManagedAt(_time, i) = ManagedAt(_teamTime, player->TeamIndex());
            }
        }

        if (_mode == GameMode::Battle
            || _mode == GameMode::BattleTeams
            || _mode == GameMode::Capture
            || _mode == GameMode::Bounty
            || _mode == GameMode::BountyTeams
            || _mode == GameMode::Nodes
            || _mode == GameMode::NodesTeams)
        {
            const std::int32_t teamIndex = MainPlayer()->TeamIndex();
            const std::int32_t teamPoints = ManagedAt(_teamPoints, teamIndex);
            if (teamPoints != ManagedAt(prevTeamPoints, teamIndex)
                && teamPoints == UncheckedSubtract(_pointGoal, 1))
            {
                Sound::Sfx::QueueStream(
                    VoiceId::VOICE_ONE_KILL_TO_WIN, 1.0F);
            }
        }
        else if (_mode == GameMode::Survival
            || _mode == GameMode::SurvivalTeams)
        {
            std::int32_t opponents = 0;
            std::int32_t lastTeam = -1;
            std::int32_t opponentMask = 0;
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(SlotCapacity); ++i)
            {
                auto player = PlayerAt(i);
                if (!HasLoadFlag(player->LoadFlags(), Entities::LoadFlags::Active))
                {
                    continue;
                }
                if (player->Health() > 0
                    || ManagedAt(_teamDeaths, player->TeamIndex()) <= _pointGoal)
                {
                    if (player->TeamIndex() != MainPlayer()->TeamIndex()
                        && (opponentMask & (1 << player->TeamIndex())) == 0)
                    {
                        opponentMask |= 1 << player->TeamIndex();
                        opponents = UncheckedAdd(opponents, 1);
                        lastTeam = player->TeamIndex();
                    }
                }
                if (ManagedAt(_teamDeaths, player->TeamIndex()) > _pointGoal
                    && player->RespawnTimer() == Entities::PlayerEntity::RespawnTime())
                {
                    Sound::Sfx::QueueStream(VoiceId::VOICE_ELIMINATED);
                }
            }

            if (HasLoadFlag(MainPlayer()->LoadFlags(), Entities::LoadFlags::Active)
                && opponents == 1 && lastTeam != -1)
            {
                const std::int32_t teamDeaths = ManagedAt(_teamDeaths, lastTeam);
                if (teamDeaths != ManagedAt(prevTeamDeaths, lastTeam)
                    && teamDeaths == _pointGoal)
                {
                    Sound::Sfx::QueueStream(
                        VoiceId::VOICE_ONE_KILL_TO_WIN, 1.0F);
                }
            }
        }

        UpdateStandings();
        // todo: update license info
    }

    std::int32_t GameState::ComparePlayers(
        std::int32_t slot1, std::int32_t slot2)
    {
        const std::int32_t points1 = ManagedAt(_points, slot1);
        const std::int32_t points2 = ManagedAt(_points, slot2);
        float time1 = ManagedAt(_time, slot1);
        float time2 = ManagedAt(_time, slot2);
        if (_mode == GameMode::Survival
            || _mode == GameMode::SurvivalTeams)
        {
            if (time1 == -1.0F)
            {
                time1 = std::numeric_limits<float>::max();
            }
            if (time2 == -1.0F)
            {
                time2 = std::numeric_limits<float>::max();
            }
        }
        const std::int32_t deaths1 = ManagedAt(_deaths, slot1);
        const std::int32_t deaths2 = ManagedAt(_deaths, slot2);
        const std::int32_t kills1 = ManagedAt(_kills, slot1);
        const std::int32_t kills2 = ManagedAt(_kills, slot2);

        if (_mode == GameMode::Battle
            || _mode == GameMode::BattleTeams)
        {
            if (points1 == points2 && deaths1 == deaths2) return 0;
            if (points1 < points2 || (points1 == points2 && deaths1 > deaths2)) return -1;
            return 1;
        }
        if (_mode == GameMode::Survival
            || _mode == GameMode::SurvivalTeams)
        {
            if (time1 == time2 && deaths1 == deaths2) return 0;
            if (time1 < time2 || (time1 == time2 && deaths1 > deaths2)) return -1;
            return 1;
        }
        if (_mode == GameMode::Defender
            || _mode == GameMode::DefenderTeams)
        {
            if (time1 == time2 && kills1 == kills2) return 0;
            if (time1 < time2 || (time1 == time2 && kills1 < kills2)) return -1;
            return 1;
        }
        if (_mode == GameMode::Capture
            || _mode == GameMode::Nodes
            || _mode == GameMode::NodesTeams
            || _mode == GameMode::Bounty
            || _mode == GameMode::BountyTeams)
        {
            if (points1 == points2 && kills1 == kills2) return 0;
            if (points1 < points2 || (points1 == points2 && kills1 < kills2)) return -1;
            return 1;
        }
        if (_mode == GameMode::PrimeHunter)
        {
            if (time1 == time2 && kills1 == kills2) return 0;
            if (time1 < time2 || (time1 == time2 && kills1 < kills2)) return -1;
            return 1;
        }
        return 0;
    }

    std::int32_t GameState::CompareTeams(
        std::int32_t slot1, std::int32_t slot2)
    {
        const std::int32_t points1 = ManagedAt(_teamPoints, slot1);
        const std::int32_t points2 = ManagedAt(_teamPoints, slot2);
        float time1 = ManagedAt(_teamTime, slot1);
        float time2 = ManagedAt(_teamTime, slot2);
        if (_mode == GameMode::Survival
            || _mode == GameMode::SurvivalTeams)
        {
            if (time1 == -1.0F) time1 = std::numeric_limits<float>::max();
            if (time2 == -1.0F) time2 = std::numeric_limits<float>::max();
        }
        const std::int32_t deaths1 = ManagedAt(_teamDeaths, slot1);
        const std::int32_t deaths2 = ManagedAt(_teamDeaths, slot2);
        const std::int32_t kills1 = ManagedAt(_teamKills, slot1);
        const std::int32_t kills2 = ManagedAt(_teamKills, slot2);

        if (_mode == GameMode::BattleTeams)
        {
            if (points1 == points2 && deaths1 == deaths2) return 0;
            if (points1 < points2 || (points1 == points2 && deaths1 > deaths2)) return -1;
            return 1;
        }
        if (_mode == GameMode::SurvivalTeams)
        {
            if (time1 == time2 && deaths1 == deaths2) return 0;
            if (time1 < time2 || (time1 == time2 && deaths1 > deaths2)) return -1;
            return 1;
        }
        if (_mode == GameMode::DefenderTeams)
        {
            if (time1 == time2 && kills1 == kills2) return 0;
            if (time1 < time2 || (time1 == time2 && kills1 < kills2)) return -1;
            return 1;
        }
        if (_mode == GameMode::Capture
            || _mode == GameMode::NodesTeams
            || _mode == GameMode::BountyTeams)
        {
            if (points1 == points2 && kills1 == kills2) return 0;
            if (points1 < points2 || (points1 == points2 && kills1 < kills2)) return -1;
            return 1;
        }
        return 0;
    }

    void GameState::CompleteRandomEncounter(std::int32_t roomId)
    {
        if (roomId >= 27 && roomId <= 92)
        {
            ManagedAt(_completedRandomEncounterRooms, UncheckedSubtract(roomId, 27)) = true;
        }
    }

    void GameState::UpdateCleanSave(bool force)
    {
        if (!force && _escapeTimer != -1.0F && _escapeState == MphRead::EscapeState::Escape)
        {
            return;
        }
        RequireShared(StorySave)->CopyTo(RequireShared(_cleanStorySave).get());
    }

    void GameState::RestoreCleanSave()
    {
        const auto& storySave = RequireShared(StorySave);
        const std::uint16_t prevFoundOctos = storySave->FoundOctoliths;
        const std::uint16_t prevCurOctos = storySave->CurrentOctoliths;
        const std::uint32_t prevLostOctos = storySave->LostOctoliths;

        const auto& areaHunters = RequireShared(storySave->AreaHunters);
        std::vector<std::uint8_t> prevAreaHunters(areaHunters->Length());
        for (std::size_t i = 0; i < areaHunters->Length(); ++i)
        {
            prevAreaHunters[i] = (*areaHunters)[i];
        }

        RequireShared(_cleanStorySave)->CopyTo(storySave.get());

        std::int32_t curCurCount = 0;
        std::int32_t prevCurCount = 0;
        for (std::int32_t i = 0; i < 8; ++i)
        {
            if ((storySave->CurrentOctoliths
                & static_cast<std::uint16_t>(ShiftLeftInt32(1, i))) != 0)
            {
                ++curCurCount;
            }
            if ((prevCurOctos
                & static_cast<std::uint16_t>(ShiftLeftInt32(1, i))) != 0)
            {
                ++prevCurCount;
            }
        }
        if (curCurCount > prevCurCount)
        {
            storySave->FoundOctoliths = prevFoundOctos;
            storySave->CurrentOctoliths = prevCurOctos;
            storySave->LostOctoliths = prevLostOctos;
        }

        if (!storySave->AreaHunters)
        {
            throw System::ArgumentNullException("array");
        }
        const auto& destination = storySave->AreaHunters;
        if (prevAreaHunters.size() > destination->Length())
        {
            throw std::invalid_argument("Destination array was not long enough.");
        }
        for (std::size_t i = 0; i < prevAreaHunters.size(); ++i)
        {
            (*destination)[i] = prevAreaHunters[i];
        }
    }

    std::string GameState::GetSavePath(std::uint8_t slot)
    {
        std::string number(3, '0');
        number[0] = static_cast<char>('0' + slot / 100);
        number[1] = static_cast<char>('0' + (slot / 10) % 10);
        number[2] = static_cast<char>('0' + slot % 10);
        return Paths::Combine("Savedata", "save" + number + ".json");
    }

    std::string GameState::GetSettingsPath()
    {
        return Paths::Combine("Savedata", "settings.json");
    }

    void GameState::LoadSave()
    {
        StorySave = ReadSave();
    }

    void GameState::StartNewSave()
    {
        StorySave = std::make_shared<StorySaveValue>();
    }

    std::shared_ptr<GameState::StorySaveValue> GameState::ReadSave()
    {
        std::shared_ptr<StorySaveValue> save{};
        if (::MphRead::Menu::SaveSlot != 0)
        {
            const std::string path = GetSavePath(::MphRead::Menu::SaveSlot);
            if (NativeRuntime::FileExists(path))
            {
                save = MphRead::GameStateDetail::DeserializeStorySave(
                    NativeRuntime::FileReadAllText(path));
            }
        }
        Mods::DebugLog::Line("save", "read slot " + std::to_string(::MphRead::Menu::SaveSlot) + ": "
            + (save == nullptr ? std::string("no file, new game")
                : "artifacts=0x" + HexUpper(save->Artifacts)
                    + " checkpoint room=" + std::to_string(save->CheckpointRoomId)));
        return save ? std::move(save) : std::make_shared<StorySaveValue>();
    }

    bool GameState::SaveExists(std::uint8_t slot)
    {
        return slot != 0 && NativeRuntime::FileExists(GetSavePath(slot));
    }

    std::shared_ptr<GameState::StorySaveValue> GameState::PeekSave(std::uint8_t slot)
    {
        if (!SaveExists(slot))
        {
            return nullptr;
        }
        try
        {
            return MphRead::GameStateDetail::DeserializeStorySave(
                NativeRuntime::FileReadAllText(GetSavePath(slot)));
        }
        catch (const std::exception&)
        {
            return nullptr;
        }
    }

    void GameState::CommitSave()
    {
        if (::MphRead::Menu::SaveSlot == 0)
        {
            return;
        }
        if (!NativeRuntime::DirectoryExists("Savedata"))
        {
            NativeRuntime::DirectoryCreateDirectory("Savedata");
        }

        const auto& storySave = RequireShared(StorySave);
        storySave->Weapons &= static_cast<std::uint16_t>(0xFF);

        if (ManagedAt(storySave->WeaponSlots, 2)
            == static_cast<std::int32_t>(BeamType::OmegaCannon))
        {
            ManagedAt(storySave->WeaponSlots, 2)
                = static_cast<std::int32_t>(BeamType::None);
        }

        for (std::int32_t r = 91; r <= 92; ++r)
        {
            auto& row = ManagedAt(storySave->RoomState, UncheckedSubtract(r, 27));
            for (std::int32_t b = 0; b < 60; ++b)
            {
                ManagedAt(row, b) = 0;
            }
        }

        NativeRuntime::FileWriteAllText(
            GetSavePath(::MphRead::Menu::SaveSlot),
            MphRead::GameStateDetail::SerializeStorySave(storySave));
        Mods::DebugLog::Line("save", "wrote slot " + std::to_string(::MphRead::Menu::SaveSlot)
            + ": artifacts=0x" + HexUpper(storySave->Artifacts)
            + " checkpoint room=" + std::to_string(storySave->CheckpointRoomId));
    }

    std::shared_ptr<MenuSettings> GameState::LoadSettings()
    {
        const std::string path = GetSettingsPath();
        if (NativeRuntime::FileExists(path))
        {
            std::shared_ptr<const std::unordered_map<std::string, std::string>> features{};
            std::shared_ptr<MenuSettings> menuSettings{};
            MphRead::GameStateDetail::DeserializeSettings(
                NativeRuntime::FileReadAllText(path), features, menuSettings);
            if (features)
            {
                Features::Load(*features);
            }
            if (menuSettings)
            {
                return menuSettings;
            }
        }
        return MphRead::GameStateDetail::NewMenuSettings();
    }

    void GameState::CommitSettings(
        const std::shared_ptr<MenuSettings>& menuSettings)
    {
        if (!NativeRuntime::DirectoryExists("Savedata"))
        {
            NativeRuntime::DirectoryCreateDirectory("Savedata");
        }
        const std::unordered_map<std::string, std::string> features
            = Features::Commit();
        NativeRuntime::FileWriteAllText(
            GetSettingsPath(),
            MphRead::GameStateDetail::SerializeSettings(features, menuSettings));
    }

    void GameState::Reset()
    {
        _cleanStorySave = std::make_shared<StorySaveValue>();
        LoadSave();
        CommitSave();
        UpdateCleanSave(true);

        _matchState = MphRead::MatchState::InProgress;
        _transitionState = MphRead::TransitionState::None;
        _transitionRoomId = -1;
        _transitionAltForm = false;
        _activePlayers = 0;

        const bool keepNames = Mods::Network::NetSession::Active();
        for (std::int32_t i = 0;
             i < Entities::PlayerEntity::SlotCapacity; ++i)
        {
            if (!keepNames)
            {
                ManagedAt(_nicknames, i) = "Player" + std::to_string(i + 1);
            }
            ManagedAt(_stars, i) = 0;
            ManagedAt(_standings, i) = 0;
            ManagedAt(_teamStandings, i) = 0;
            ManagedAt(_resultSlots, i) = 0;
            ManagedAt(_points, i) = 0;
            ManagedAt(_teamPoints, i) = 0;
            ManagedAt(_kills, i) = 0;
            ManagedAt(_teamKills, i) = 0;
            ManagedAt(_deaths, i) = 0;
            ManagedAt(_teamDeaths, i) = 0;
            ManagedAt(_time, i) = 0.0F;
            ManagedAt(_teamTime, i) = 0.0F;
            ManagedAt(_beamDamageMax, i) = 0;
            ManagedAt(_beamDamageDealt, i) = 0;
            ManagedAt(_damageCount, i) = 0;
            ManagedAt(_altDamageCount, i) = 0;
            ManagedAt(_kills, i) = 0;
            ManagedAt(_suicides, i) = 0;
            ManagedAt(_friendlyKills, i) = 0;
            ManagedAt(_headshotKills, i) = 0;
            ManagedAt(_octolithScores, i) = 0;
            ManagedAt(_octolithDrops, i) = 0;
            ManagedAt(_octolithStops, i) = 0;
            ManagedAt(_nodesCaptured, i) = 0;
            ManagedAt(_nodesLost, i) = 0;
            ManagedAt(_killsAsPrime, i) = 0;
            ManagedAt(_primesKilled, i) = 0;
            for (std::int32_t j = 0; j < 9; ++j)
            {
                ManagedAt(ManagedAt(_beamKills, i), j) = 0;
            }
        }

        _primeHunter = -1;
        _teams = false;
        _teamCount = 2;
        _friendlyFire = false;
        _pointGoal = 0;
        _timeGoal = 0.0F;
        _octolithReset = false;
        _radarPlayers = false;
        _affinityWeapons = false;
        _shadowFreeze = true;
        _matchTime = -1.0F;

        Entities::PlayerEntity::Reset();
        Entities::CamSeqEntity::Current(nullptr);
        Formats::CameraSequence::Current(nullptr);
        _menuPause = false;
        _dialogPause = false;
        _pausingDialog = false;
        _unpausingDialog = false;
        _pausePrevented = false;
        _escapeState = MphRead::EscapeState::None;
        _escapeTimer = -1.0F;
        _escapePaused = false;
        _queuedOctolithMessageId = -1;
        _queuedOublietteUnlockMessage = false;
    }
}


namespace MphRead::SceneSetupInterop
{
    GameMode GameModeNone()
    {
        return GameMode::None;
    }

    GameMode GameModeSinglePlayer()
    {
        return GameMode::SinglePlayer;
    }

    GameMode GameModeBattle()
    {
        return GameMode::Battle;
    }

    GameMode GameModeBounty()
    {
        return GameMode::Bounty;
    }

    GameMode GameModeCapture()
    {
        return GameMode::Capture;
    }

    GameMode GameModeNodes()
    {
        return GameMode::Nodes;
    }

    GameMode GameModeNodesTeams()
    {
        return GameMode::NodesTeams;
    }

    GameMode GameModeDefender()
    {
        return GameMode::Defender;
    }

    GameMode GameModeDefenderTeams()
    {
        return GameMode::DefenderTeams;
    }

    AreaState AreaStateClear()
    {
        return AreaState::Clear;
    }

    GameMode GetGameMode()
    {
        return GameState::Mode();
    }

    void SetGameMode(GameMode mode)
    {
        GameState::Mode(mode);
    }

    bool Multiplayer()
    {
        return GameState::Multiplayer();
    }

    bool SinglePlayer()
    {
        return GameState::SinglePlayer();
    }

    StorySave* CurrentStorySave()
    {
        return GameState::StorySave.get();
    }

    AreaState GetAreaState(std::int32_t areaId, StorySave* save)
    {
        return GameState::GetAreaState(areaId, save);
    }

    std::span<std::int32_t> EncounterState()
    {
        auto& values = GameState::EncounterState();
        return std::span<std::int32_t>(values.data(), values.size());
    }

    std::span<bool> CompletedRandomEncounterRooms()
    {
        auto& values = GameState::CompletedRandomEncounterRooms();
        return std::span<bool>(values.data(), values.size());
    }

    std::uint32_t BossFlagsValue(const StorySave& save)
    {
        return static_cast<std::uint32_t>(save.BossFlags);
    }

    std::uint32_t LostOctoliths(const StorySave& save)
    {
        return save.LostOctoliths;
    }

    std::span<std::uint8_t> AreaHunters(StorySave& save)
    {
        const auto& values = RequireShared(save.AreaHunters);
        if (values->Length() == 0)
        {
            return {};
        }
        return std::span<std::uint8_t>(
            &(*values)[0], values->Length());
    }

    std::uint8_t DefeatedHunters(const StorySave& save)
    {
        return save.DefeatedHunters;
    }

    std::int32_t GetEnemyOctolithDrop(
        const StorySave& save, std::int32_t hunter)
    {
        return save.GetEnemyOctolithDrop(hunter);
    }

    void SetCheckpointRoomId(StorySave& save, std::int32_t roomId)
    {
        save.CheckpointRoomId = roomId;
    }
}


namespace MphRead::Mods::Network::Detail
{
    bool NetMatchEndGameStateMatchInProgress()
    {
        return GameState::MatchState() == MphRead::MatchState::InProgress;
    }

    void NetMatchEndSetGameStateMatchTime(float value)
    {
        GameState::MatchTime(value);
    }

    void NetMatchEndGameStateResetMatchProgress()
    {
        GameState::ResetMatchProgress();
    }

    double DedicatedServerMatchEndingSeconds()
    {
        return static_cast<double>(GameState::MatchEndingSeconds);
    }
}

namespace MphRead
{
    namespace
    {
        // GameState.cs MatchState : int
        constexpr ::MphRead::NativeRuntime::EnumNameEntry MatchStateNames[] = {
            {0x0ULL, "InProgress"},
            {0x1ULL, "GameOver"},
            {0x2ULL, "Ending"},
            {0x3ULL, "Disconnected"},
        };
    }

    std::string ToString(MatchState value)
    {
        return ::MphRead::NativeRuntime::ManagedEnumToString(
            value, MatchStateNames, std::size(MatchStateNames), false);
    }
}

namespace MphRead::GameStateDetail
{
    // System.Text.Json over the two documents GameState.cs writes: a save slot
    // and the settings file. Each property is named here because C++ has no
    // reflection to walk them with.
    namespace
    {
        using Json = ::MphRead::NativeRuntime::JsonValue;
        using JsonPtr = ::MphRead::NativeRuntime::JsonPtr;

        void ReadString(const JsonPtr& object, const char* name, std::string& target)
        {
            const JsonPtr member = object->Get(name);
            if (member != nullptr && member->Type() == Json::Kind::String)
            {
                target = member->Text();
            }
        }

        void ReadInt(const JsonPtr& object, const char* name, std::int32_t& target)
        {
            const JsonPtr member = object->Get(name);
            if (member != nullptr && member->Type() == Json::Kind::Number)
            {
                target = static_cast<std::int32_t>(member->AsInt64(target));
            }
        }

        void ReadUInt16(const JsonPtr& object, const char* name, std::uint16_t& target)
        {
            const JsonPtr member = object->Get(name);
            if (member != nullptr && member->Type() == Json::Kind::Number)
            {
                target = static_cast<std::uint16_t>(member->AsInt64(target));
            }
        }

        void ReadUInt32(const JsonPtr& object, const char* name, std::uint32_t& target)
        {
            const JsonPtr member = object->Get(name);
            if (member != nullptr && member->Type() == Json::Kind::Number)
            {
                target = static_cast<std::uint32_t>(member->AsInt64(target));
            }
        }

        template <typename TArray>
        void ReadNumberArray(const JsonPtr& object, const char* name, TArray& target)
        {
            const JsonPtr member = object->Get(name);
            if (member == nullptr || member->Type() != Json::Kind::Array || !target)
            {
                return;
            }
            const std::size_t count = std::min(target->Length(), member->Items().size());
            for (std::size_t i = 0; i < count; ++i)
            {
                using Element = std::remove_reference_t<decltype((*target)[i])>;
                (*target)[i] = static_cast<std::remove_cv_t<Element>>(
                    member->Items()[i]->AsInt64(0));
            }
        }

        void ReadByteArray(const JsonPtr& object, const char* name,
            StorySave::ByteArray& target)
        {
            ReadNumberArray(object, name, target);
        }

        void ReadIntArray(const JsonPtr& object, const char* name,
            StorySave::IntArray& target)
        {
            ReadNumberArray(object, name, target);
        }

        void ReadJagged(const JsonPtr& object, const char* name,
            StorySave::ByteJaggedArray& target)
        {
            const JsonPtr member = object->Get(name);
            if (member == nullptr || member->Type() != Json::Kind::Array || !target)
            {
                return;
            }
            const std::size_t rows = std::min(target->Length(), member->Items().size());
            for (std::size_t row = 0; row < rows; ++row)
            {
                StorySave::ByteArray& inner = (*target)[row];
                const JsonPtr source = member->Items()[row];
                if (!inner || source == nullptr || source->Type() != Json::Kind::Array)
                {
                    continue;
                }
                const std::size_t count = std::min(inner->Length(), source->Items().size());
                for (std::size_t i = 0; i < count; ++i)
                {
                    (*inner)[i] = static_cast<std::uint8_t>(source->Items()[i]->AsInt64(0));
                }
            }
        }

        [[nodiscard]] JsonPtr WriteByteArray(const StorySave::ByteArray& value)
        {
            JsonPtr array = Json::MakeArray();
            if (value)
            {
                for (std::size_t i = 0; i < value->Length(); ++i)
                {
                    array->Items().push_back(Json::MakeNumber(
                        std::to_string(static_cast<std::int32_t>((*value)[i]))));
                }
            }
            return array;
        }

        [[nodiscard]] JsonPtr WriteIntArray(const StorySave::IntArray& value)
        {
            JsonPtr array = Json::MakeArray();
            if (value)
            {
                for (std::size_t i = 0; i < value->Length(); ++i)
                {
                    array->Items().push_back(Json::MakeNumber(std::to_string((*value)[i])));
                }
            }
            return array;
        }

        [[nodiscard]] JsonPtr WriteJagged(const StorySave::ByteJaggedArray& value)
        {
            JsonPtr array = Json::MakeArray();
            if (value)
            {
                for (std::size_t row = 0; row < value->Length(); ++row)
                {
                    array->Items().push_back(WriteByteArray((*value)[row]));
                }
            }
            return array;
        }
    }

    std::shared_ptr<StorySave> DeserializeStorySave(const std::string& json)
    {
        const JsonPtr object = ::MphRead::NativeRuntime::JsonParse(json);
        if (object == nullptr || object->Type() != Json::Kind::Object)
        {
            return nullptr;
        }
        auto save = std::make_shared<StorySave>();
        ReadInt(object, "ScanCount", save->ScanCount);
        ReadInt(object, "EquipmentCount", save->EquipmentCount);
        ReadInt(object, "CheckpointEntityId", save->CheckpointEntityId);
        ReadInt(object, "CheckpointRoomId", save->CheckpointRoomId);
        ReadInt(object, "Health", save->Health);
        ReadInt(object, "HealthMax", save->HealthMax);
        ReadUInt16(object, "Weapons", save->Weapons);
        ReadUInt16(object, "FoundOctoliths", save->FoundOctoliths);
        ReadUInt16(object, "CurrentOctoliths", save->CurrentOctoliths);
        ReadUInt16(object, "Areas", save->Areas);
        ReadUInt32(object, "Artifacts", save->Artifacts);
        ReadUInt32(object, "LostOctoliths", save->LostOctoliths);
        ReadByteArray(object, "VisitedRooms", save->VisitedRooms);
        ReadByteArray(object, "TriggerState", save->TriggerState);
        ReadByteArray(object, "Logbook", save->Logbook);
        ReadByteArray(object, "AreaHunters", save->AreaHunters);
        ReadIntArray(object, "VisitedConnectors", save->VisitedConnectors);
        ReadIntArray(object, "Ammo", save->Ammo);
        ReadIntArray(object, "AmmoMax", save->AmmoMax);
        ReadIntArray(object, "WeaponSlots", save->WeaponSlots);
        ReadJagged(object, "RoomState", save->RoomState);
        ReadJagged(object, "EnemyEncounters", save->EnemyEncounters);
        const JsonPtr stats = object->Get("Stats");
        if (stats != nullptr && stats->Type() == Json::Kind::Object && save->Stats)
        {
            const JsonPtr kills = stats->Get("HunterKills");
            const JsonPtr deaths = stats->Get("Deaths");
            const JsonPtr enemyDeaths = stats->Get("EnemyHunterDeaths");
            const JsonPtr enemyKills = stats->Get("EnemyKills");
            if (kills != nullptr)
            {
                save->Stats->HunterKills = static_cast<std::uint32_t>(kills->AsInt64(0));
            }
            if (deaths != nullptr)
            {
                save->Stats->Deaths = static_cast<std::uint32_t>(deaths->AsInt64(0));
            }
            if (enemyDeaths != nullptr)
            {
                save->Stats->EnemyHunterDeaths
                    = static_cast<std::uint32_t>(enemyDeaths->AsInt64(0));
            }
            if (enemyKills != nullptr)
            {
                save->Stats->EnemyKills = static_cast<std::uint32_t>(enemyKills->AsInt64(0));
            }
        }
        const JsonPtr bossFlags = object->Get("BossFlags");
        if (bossFlags != nullptr)
        {
            save->BossFlags = static_cast<StorySave::BossFlagsValue>(bossFlags->AsInt64(0));
        }
        const JsonPtr defeated = object->Get("DefeatedHunters");
        if (defeated != nullptr)
        {
            save->DefeatedHunters = static_cast<std::uint8_t>(defeated->AsInt64(0));
        }
        return save;
    }

    std::string SerializeStorySave(const std::shared_ptr<StorySave>& save)
    {
        if (!save)
        {
            return "null";
        }
        JsonPtr object = Json::MakeObject();
        object->Set("ScanCount", Json::MakeNumber(std::to_string(save->ScanCount)));
        object->Set("EquipmentCount", Json::MakeNumber(std::to_string(save->EquipmentCount)));
        object->Set("CheckpointEntityId", Json::MakeNumber(std::to_string(save->CheckpointEntityId)));
        object->Set("CheckpointRoomId", Json::MakeNumber(std::to_string(save->CheckpointRoomId)));
        object->Set("Health", Json::MakeNumber(std::to_string(save->Health)));
        object->Set("HealthMax", Json::MakeNumber(std::to_string(save->HealthMax)));
        object->Set("Weapons", Json::MakeNumber(std::to_string(save->Weapons)));
        object->Set("FoundOctoliths", Json::MakeNumber(std::to_string(save->FoundOctoliths)));
        object->Set("CurrentOctoliths", Json::MakeNumber(std::to_string(save->CurrentOctoliths)));
        object->Set("Areas", Json::MakeNumber(std::to_string(save->Areas)));
        object->Set("Artifacts", Json::MakeNumber(std::to_string(save->Artifacts)));
        object->Set("LostOctoliths", Json::MakeNumber(std::to_string(save->LostOctoliths)));
        object->Set("VisitedRooms", WriteByteArray(save->VisitedRooms));
        object->Set("TriggerState", WriteByteArray(save->TriggerState));
        object->Set("Logbook", WriteByteArray(save->Logbook));
        object->Set("AreaHunters", WriteByteArray(save->AreaHunters));
        object->Set("VisitedConnectors", WriteIntArray(save->VisitedConnectors));
        object->Set("Ammo", WriteIntArray(save->Ammo));
        object->Set("AmmoMax", WriteIntArray(save->AmmoMax));
        object->Set("WeaponSlots", WriteIntArray(save->WeaponSlots));
        object->Set("RoomState", WriteJagged(save->RoomState));
        object->Set("EnemyEncounters", WriteJagged(save->EnemyEncounters));
        object->Set("BossFlags", Json::MakeNumber(
            std::to_string(static_cast<std::int32_t>(save->BossFlags))));
        object->Set("DefeatedHunters", Json::MakeNumber(
            std::to_string(static_cast<std::int32_t>(save->DefeatedHunters))));
        JsonPtr stats = Json::MakeObject();
        if (save->Stats)
        {
            stats->Set("HunterKills", Json::MakeNumber(std::to_string(save->Stats->HunterKills)));
            stats->Set("Deaths", Json::MakeNumber(std::to_string(save->Stats->Deaths)));
            stats->Set("EnemyHunterDeaths",
                Json::MakeNumber(std::to_string(save->Stats->EnemyHunterDeaths)));
            stats->Set("EnemyKills", Json::MakeNumber(std::to_string(save->Stats->EnemyKills)));
        }
        object->Set("Stats", stats);
        return ::MphRead::NativeRuntime::JsonWrite(object);
    }

    std::shared_ptr<MenuSettings> NewMenuSettings()
    {
        return std::make_shared<MenuSettings>();
    }

    void DeserializeSettings(const std::string& json,
        std::shared_ptr<const std::unordered_map<std::string, std::string>>& features,
        std::shared_ptr<MenuSettings>& menuSettings)
    {
        features = nullptr;
        menuSettings = nullptr;
        const JsonPtr object = ::MphRead::NativeRuntime::JsonParse(json);
        if (object == nullptr || object->Type() != Json::Kind::Object)
        {
            return;
        }
        const JsonPtr featureObject = object->Get("Features");
        if (featureObject != nullptr && featureObject->Type() == Json::Kind::Object)
        {
            auto map = std::make_shared<std::unordered_map<std::string, std::string>>();
            for (const auto& member : featureObject->Members())
            {
                if (member.second != nullptr && member.second->Type() == Json::Kind::String)
                {
                    map->emplace(member.first, member.second->Text());
                }
            }
            features = map;
        }
        const JsonPtr object2 = object->Get("MenuSettings");
        if (object2 != nullptr && object2->Type() == Json::Kind::Object)
        {
            auto value = std::make_shared<MenuSettings>();
            ReadString(object2, "RoomKey", value->RoomKey);
            ReadString(object2, "Mode", value->Mode);
            ReadString(object2, "Player1", value->Player1);
            ReadString(object2, "Player2", value->Player2);
            ReadString(object2, "Player3", value->Player3);
            ReadString(object2, "Player4", value->Player4);
            ReadString(object2, "Models", value->Models);
            ReadString(object2, "MphVersion", value->MphVersion);
            ReadString(object2, "FhVersion", value->FhVersion);
            ReadString(object2, "Language", value->Language);
            ReadString(object2, "SfxVolume", value->SfxVolume);
            ReadString(object2, "MusicVolume", value->MusicVolume);
            ReadString(object2, "ResolutionScale", value->ResolutionScale);
            ReadString(object2, "FieldOfView", value->FieldOfView);
            ReadString(object2, "Lighting", value->Lighting);
            ReadString(object2, "Fog", value->Fog);
            ReadString(object2, "TextureFiltering", value->TextureFiltering);
            ReadString(object2, "ShowFps", value->ShowFps);
            ReadString(object2, "FrameRateCap", value->FrameRateCap);
            ReadString(object2, "VSync", value->VSync);
            ReadString(object2, "CelShading", value->CelShading);
            ReadString(object2, "PerformanceMode", value->PerformanceMode);
            ReadString(object2, "CelBands", value->CelBands);
            ReadString(object2, "CelEdge", value->CelEdge);
            ReadString(object2, "PointGoal", value->PointGoal);
            ReadString(object2, "TimeLimit", value->TimeLimit);
            ReadString(object2, "TimeGoal", value->TimeGoal);
            ReadString(object2, "AutoReset", value->AutoReset);
            ReadString(object2, "TeamPlay", value->TeamPlay);
            ReadString(object2, "HunterRadar", value->HunterRadar);
            ReadString(object2, "DamageLevel", value->DamageLevel);
            ReadString(object2, "FriendlyFire", value->FriendlyFire);
            ReadString(object2, "AffinityWeapons", value->AffinityWeapons);
            ReadString(object2, "ShadowFreeze", value->ShadowFreeze);
            ReadString(object2, "SaveSlot", value->SaveSlot);
            ReadString(object2, "SaveFromExit", value->SaveFromExit);
            ReadString(object2, "SaveFromShip", value->SaveFromShip);
            ReadString(object2, "Planets", value->Planets);
            ReadString(object2, "Alinos1State", value->Alinos1State);
            ReadString(object2, "Alinos2State", value->Alinos2State);
            ReadString(object2, "Ca1State", value->Ca1State);
            ReadString(object2, "Ca2State", value->Ca2State);
            ReadString(object2, "Vdo1State", value->Vdo1State);
            ReadString(object2, "Vdo2State", value->Vdo2State);
            ReadString(object2, "Arcterra1State", value->Arcterra1State);
            ReadString(object2, "Arcterra2State", value->Arcterra2State);
            ReadString(object2, "CheckpointId", value->CheckpointId);
            ReadString(object2, "HealthMax", value->HealthMax);
            ReadString(object2, "MissileMax", value->MissileMax);
            ReadString(object2, "UaMax", value->UaMax);
            ReadString(object2, "Weapons", value->Weapons);
            ReadString(object2, "Octoliths", value->Octoliths);
            menuSettings = value;
        }
    }

    std::string SerializeSettings(
        const std::unordered_map<std::string, std::string>& features,
        const std::shared_ptr<MenuSettings>& menuSettings)
    {
        JsonPtr root = Json::MakeObject();
        JsonPtr featureObject = Json::MakeObject();
        for (const auto& item : features)
        {
            featureObject->Set(item.first, Json::MakeString(item.second));
        }
        root->Set("Features", featureObject);
        if (!menuSettings)
        {
            root->Set("MenuSettings", Json::MakeNull());
            return ::MphRead::NativeRuntime::JsonWrite(root);
        }
        JsonPtr object = Json::MakeObject();
        const std::shared_ptr<MenuSettings>& value = menuSettings;
        object->Set("RoomKey", Json::MakeString(value->RoomKey));
        object->Set("Mode", Json::MakeString(value->Mode));
        object->Set("Player1", Json::MakeString(value->Player1));
        object->Set("Player2", Json::MakeString(value->Player2));
        object->Set("Player3", Json::MakeString(value->Player3));
        object->Set("Player4", Json::MakeString(value->Player4));
        object->Set("Models", Json::MakeString(value->Models));
        object->Set("MphVersion", Json::MakeString(value->MphVersion));
        object->Set("FhVersion", Json::MakeString(value->FhVersion));
        object->Set("Language", Json::MakeString(value->Language));
        object->Set("SfxVolume", Json::MakeString(value->SfxVolume));
        object->Set("MusicVolume", Json::MakeString(value->MusicVolume));
        object->Set("ResolutionScale", Json::MakeString(value->ResolutionScale));
        object->Set("FieldOfView", Json::MakeString(value->FieldOfView));
        object->Set("Lighting", Json::MakeString(value->Lighting));
        object->Set("Fog", Json::MakeString(value->Fog));
        object->Set("TextureFiltering", Json::MakeString(value->TextureFiltering));
        object->Set("ShowFps", Json::MakeString(value->ShowFps));
        object->Set("FrameRateCap", Json::MakeString(value->FrameRateCap));
        object->Set("VSync", Json::MakeString(value->VSync));
        object->Set("CelShading", Json::MakeString(value->CelShading));
        object->Set("PerformanceMode", Json::MakeString(value->PerformanceMode));
        object->Set("CelBands", Json::MakeString(value->CelBands));
        object->Set("CelEdge", Json::MakeString(value->CelEdge));
        object->Set("PointGoal", Json::MakeString(value->PointGoal));
        object->Set("TimeLimit", Json::MakeString(value->TimeLimit));
        object->Set("TimeGoal", Json::MakeString(value->TimeGoal));
        object->Set("AutoReset", Json::MakeString(value->AutoReset));
        object->Set("TeamPlay", Json::MakeString(value->TeamPlay));
        object->Set("HunterRadar", Json::MakeString(value->HunterRadar));
        object->Set("DamageLevel", Json::MakeString(value->DamageLevel));
        object->Set("FriendlyFire", Json::MakeString(value->FriendlyFire));
        object->Set("AffinityWeapons", Json::MakeString(value->AffinityWeapons));
        object->Set("ShadowFreeze", Json::MakeString(value->ShadowFreeze));
        object->Set("SaveSlot", Json::MakeString(value->SaveSlot));
        object->Set("SaveFromExit", Json::MakeString(value->SaveFromExit));
        object->Set("SaveFromShip", Json::MakeString(value->SaveFromShip));
        object->Set("Planets", Json::MakeString(value->Planets));
        object->Set("Alinos1State", Json::MakeString(value->Alinos1State));
        object->Set("Alinos2State", Json::MakeString(value->Alinos2State));
        object->Set("Ca1State", Json::MakeString(value->Ca1State));
        object->Set("Ca2State", Json::MakeString(value->Ca2State));
        object->Set("Vdo1State", Json::MakeString(value->Vdo1State));
        object->Set("Vdo2State", Json::MakeString(value->Vdo2State));
        object->Set("Arcterra1State", Json::MakeString(value->Arcterra1State));
        object->Set("Arcterra2State", Json::MakeString(value->Arcterra2State));
        object->Set("CheckpointId", Json::MakeString(value->CheckpointId));
        object->Set("HealthMax", Json::MakeString(value->HealthMax));
        object->Set("MissileMax", Json::MakeString(value->MissileMax));
        object->Set("UaMax", Json::MakeString(value->UaMax));
        object->Set("Weapons", Json::MakeString(value->Weapons));
        object->Set("Octoliths", Json::MakeString(value->Octoliths));
        root->Set("MenuSettings", object);
        return ::MphRead::NativeRuntime::JsonWrite(root);
    }
}
