#pragma once

#include "Formats/Enums.hpp"
#include "Formats/Types.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MphRead
{
    class MenuSettings;
    class Scene;

    enum class AreaState : std::int32_t;
    enum class BossFlags : std::int32_t;
    enum class GameMode : std::uint8_t;

    enum class MatchState : std::int32_t
    {
        InProgress = 0,
        GameOver = 1,
        Ending = 2,
        Disconnected = 3
    };

    // MatchState.ToString().
    [[nodiscard]] std::string ToString(MatchState value);

    enum class TransitionState : std::int32_t
    {
        None = 0,
        Start = 1,
        Process = 2,
        End = 3
    };

    enum class EscapeState : std::int32_t
    {
        None = 0,
        Event = 1,
        Escape = 2
    };

    class StorySave
    {
    public:
        class SaveStats
        {
        public:
            std::uint32_t HunterKills = 0;
            std::uint32_t Deaths = 0;
            std::uint32_t EnemyHunterDeaths = 0;
            std::uint32_t EnemyKills = 0;
        };

        using ByteArray = std::shared_ptr<ManagedArray<std::uint8_t>>;
        using IntArray = std::shared_ptr<ManagedArray<std::int32_t>>;
        using ByteJaggedArray = std::shared_ptr<ManagedArray<ByteArray>>;

        ByteJaggedArray RoomState;
        ByteArray VisitedRooms = std::make_shared<ManagedArray<std::uint8_t>>(9);
        IntArray VisitedConnectors = std::make_shared<ManagedArray<std::int32_t>>(9);
        ByteArray TriggerState = std::make_shared<ManagedArray<std::uint8_t>>(4);
        ByteArray Logbook = std::make_shared<ManagedArray<std::uint8_t>>(68);
        ByteJaggedArray EnemyEncounters;
        std::int32_t ScanCount = 0;
        std::int32_t EquipmentCount = 0;
        std::int32_t CheckpointEntityId = -1;
        std::int32_t CheckpointRoomId = -1;
        std::int32_t Health = 0;
        std::int32_t HealthMax = 0;
        IntArray Ammo = std::make_shared<ManagedArray<std::int32_t>>(2);
        IntArray AmmoMax = std::make_shared<ManagedArray<std::int32_t>>(2);
        IntArray WeaponSlots = std::make_shared<ManagedArray<std::int32_t>>(3);
        std::uint16_t Weapons = 0;
        std::uint32_t Artifacts = 0;
        std::uint16_t FoundOctoliths = 0;
        std::uint16_t CurrentOctoliths = 0;
        std::uint32_t LostOctoliths = UINT32_MAX;
        std::uint16_t Areas = 0xC;
        using BossFlagsValue = MphRead::BossFlags;
        BossFlagsValue BossFlags = static_cast<BossFlagsValue>(0);
        ByteArray AreaHunters = std::make_shared<ManagedArray<std::uint8_t>>(4);
        std::uint8_t DefeatedHunters = 0;
        std::shared_ptr<SaveStats> Stats = std::make_shared<SaveStats>();

        StorySave();

        [[nodiscard]] std::int32_t InitRoomState(
            std::int32_t roomId, std::int32_t entityId, bool active,
            std::int32_t activeState = 3, std::int32_t inactiveState = 1);
        [[nodiscard]] std::int32_t GetRoomState(
            std::int32_t roomId, std::int32_t entityId) const;
        void SetRoomState(std::int32_t roomId, std::int32_t entityId, std::int32_t state);
        [[nodiscard]] bool CheckVisitedRoom(std::int32_t roomId) const;
        void SetVisitedRoom(std::int32_t roomId);
        [[nodiscard]] bool CheckVisitedConnector(
            std::int32_t connectorId, std::int32_t areaId) const;
        void SetVisitedConnector(std::int32_t connectorId, std::int32_t areaId);
        [[nodiscard]] bool CheckFoundOctolith(std::int32_t areaId) const;
        [[nodiscard]] std::int32_t CountFoundOctoliths() const;
        void UpdateFoundOctolith(std::int32_t areaId);
        [[nodiscard]] bool CheckFoundArtifact(
            std::int32_t artifactId, std::int32_t modelId) const;
        [[nodiscard]] std::int32_t CountFoundArtifacts(std::int32_t modelId) const;
        void UpdateFoundArtifact(std::int32_t artifactId, std::int32_t modelId);
        [[nodiscard]] std::int32_t GetEnemyOctolithDrop(std::int32_t hunter) const;
        void UpdateLogbook(std::int32_t scanId);
        [[nodiscard]] bool CheckLogbook(std::int32_t scanId) const;
        [[nodiscard]] std::int32_t GetLogbookCount(
            bool unlockedOnly,
            const std::shared_ptr<ManagedArray<char>>& categories) const;
        [[nodiscard]] std::int32_t GetMaxScanCount() const;
        [[nodiscard]] std::int32_t GetCompletionPercentage() const;
        void CopyTo(StorySave* other) const;
    };

    class GameState final
    {
    public:
        static constexpr float MatchEndingSeconds = 10.0F;
        static constexpr std::size_t SlotCapacity = 8;
        static constexpr std::size_t BeamCount = 9;

        using MatchStateValue = MphRead::MatchState;
        using TransitionStateValue = MphRead::TransitionState;
        using EscapeStateValue = MphRead::EscapeState;
        using StorySaveValue = MphRead::StorySave;
        using ModeStateAction = std::function<void(Scene*)>;
        using IntSlots = std::array<std::int32_t, SlotCapacity>;
        using FloatSlots = std::array<float, SlotCapacity>;
        using BoolRooms = std::array<bool, 66>;
        using NicknameSlots = std::array<std::string, SlotCapacity>;
        using BeamKillSlots = std::array<std::array<std::int32_t, BeamCount>, SlotCapacity>;

        GameState() = delete;

        [[nodiscard]] static GameMode Mode() noexcept;
        static void Mode(GameMode value) noexcept;
        [[nodiscard]] static bool SinglePlayer();
        [[nodiscard]] static bool Multiplayer();
        [[nodiscard]] static bool IsOctolithMode();
        [[nodiscard]] static bool PausePrevented() noexcept;
        static void PausePrevented(bool value) noexcept;
        [[nodiscard]] static bool MenuPause() noexcept;
        [[nodiscard]] static bool DialogPause() noexcept;
        [[nodiscard]] static MatchStateValue MatchState() noexcept;
        static void MatchState(MatchStateValue value) noexcept;
        [[nodiscard]] static TransitionStateValue TransitionState() noexcept;
        static void TransitionState(TransitionStateValue value) noexcept;
        [[nodiscard]] static bool InRoomTransition() noexcept;
        [[nodiscard]] static EscapeStateValue EscapeState() noexcept;
        static void EscapeState(EscapeStateValue value) noexcept;
        [[nodiscard]] static float EscapeTimer() noexcept;
        static void EscapeTimer(float value) noexcept;
        [[nodiscard]] static bool EscapePaused() noexcept;
        static void EscapePaused(bool value) noexcept;

        [[nodiscard]] static IntSlots& EncounterState() noexcept;
        [[nodiscard]] static BoolRooms& CompletedRandomEncounterRooms() noexcept;
        [[nodiscard]] static std::int32_t TransitionRoomId() noexcept;
        static void TransitionRoomId(std::int32_t value) noexcept;
        [[nodiscard]] static bool TransitionAltForm() noexcept;
        static void TransitionAltForm(bool value) noexcept;
        [[nodiscard]] static std::int32_t ActivePlayers() noexcept;
        static void ActivePlayers(std::int32_t value) noexcept;
        [[nodiscard]] static NicknameSlots& Nicknames() noexcept;
        [[nodiscard]] static IntSlots& Stars() noexcept;
        [[nodiscard]] static IntSlots& Standings() noexcept;
        [[nodiscard]] static IntSlots& TeamStandings() noexcept;
        [[nodiscard]] static IntSlots& ResultSlots() noexcept;
        [[nodiscard]] static std::int32_t PrimeHunter() noexcept;
        static void PrimeHunter(std::int32_t value) noexcept;

        [[nodiscard]] static bool Teams() noexcept;
        static void Teams(bool value) noexcept;
        [[nodiscard]] static std::int32_t TeamCount() noexcept;
        static void TeamCount(std::int32_t value) noexcept;
        [[nodiscard]] static bool FriendlyFire() noexcept;
        static void FriendlyFire(bool value) noexcept;
        [[nodiscard]] static std::int32_t PointGoal() noexcept;
        static void PointGoal(std::int32_t value) noexcept;
        [[nodiscard]] static float TimeGoal() noexcept;
        static void TimeGoal(float value) noexcept;
        // The multiplier every hit is scaled by inside TakeDamage: index into
        // Metadata::DamageLevels, 0.75 / 1 / 1.25.
        //
        // **Pinned to medium, which is x1, and nothing sets it.** It was a
        // per-machine setting read out of each player's own settings file, and
        // it multiplies the damage of *every* weapon -- so two machines that
        // disagreed about it disagreed about every shot in the match by up to
        // a third, in the one direction nothing corrects: a client resolves
        // its own hits the instant it fires them (NetHitPrediction) and the
        // authority resolves them again a round trip later, so a client
        // scaling higher runs a victim's health down faster than the machine
        // keeping score and eventually predicts a kill on somebody who is
        // standing up.
        //
        // Nobody was asking for the other two answers and the cartridge's own
        // default is the middle one, so there is one answer. **The setter
        // accepts and discards**: upstream's console menu still has a Damage
        // Level row and still assigns this, and the point is that the
        // assignment does nothing rather than that the row is edited out of a
        // file every pull from upstream has to fast-forward through.
        // GameSettings::ApplyMatchRules -- which is this project's own -- does
        // not assign it at all.
        [[nodiscard]] static std::int32_t DamageLevel() noexcept;
        static void DamageLevel(std::int32_t value) noexcept;
        [[nodiscard]] static bool OctolithReset() noexcept;
        static void OctolithReset(bool value) noexcept;
        [[nodiscard]] static bool RadarPlayers() noexcept;
        static void RadarPlayers(bool value) noexcept;
        [[nodiscard]] static bool AffinityWeapons() noexcept;
        static void AffinityWeapons(bool value) noexcept;
        [[nodiscard]] static bool ShadowFreeze() noexcept;
        static void ShadowFreeze(bool value) noexcept;
        // Instagib: everybody has the Imperialist, infinite ammo, one hit kills,
        // and nothing on the map can be picked up. A match rule: broadcast by the
        // server, so every machine plays the same one.
        [[nodiscard]] static bool Instagib() noexcept;
        static void Instagib(bool value) noexcept;
        [[nodiscard]] static float MatchTime() noexcept;
        static void MatchTime(float value) noexcept;
        [[nodiscard]] static bool ForceEndGame() noexcept;
        static void ForceEndGame(bool value) noexcept;

        [[nodiscard]] static IntSlots& Points() noexcept;
        [[nodiscard]] static IntSlots& TeamPoints() noexcept;
        [[nodiscard]] static IntSlots& Kills() noexcept;
        [[nodiscard]] static IntSlots& TeamKills() noexcept;
        [[nodiscard]] static IntSlots& Deaths() noexcept;
        [[nodiscard]] static IntSlots& TeamDeaths() noexcept;
        [[nodiscard]] static FloatSlots& Time() noexcept;
        [[nodiscard]] static FloatSlots& TeamTime() noexcept;
        [[nodiscard]] static IntSlots& BeamDamageMax() noexcept;
        [[nodiscard]] static IntSlots& BeamDamageDealt() noexcept;
        [[nodiscard]] static IntSlots& DamageCount() noexcept;
        [[nodiscard]] static IntSlots& AltDamageCount() noexcept;
        [[nodiscard]] static IntSlots& KillStreak() noexcept;
        [[nodiscard]] static IntSlots& Suicides() noexcept;
        [[nodiscard]] static IntSlots& FriendlyKills() noexcept;
        [[nodiscard]] static IntSlots& HeadshotKills() noexcept;
        [[nodiscard]] static BeamKillSlots& BeamKills() noexcept;
        [[nodiscard]] static IntSlots& OctolithScores() noexcept;
        [[nodiscard]] static IntSlots& OctolithDrops() noexcept;
        [[nodiscard]] static IntSlots& OctolithStops() noexcept;
        [[nodiscard]] static IntSlots& NodesCaptured() noexcept;
        [[nodiscard]] static IntSlots& NodesLost() noexcept;
        [[nodiscard]] static IntSlots& KillsAsPrime() noexcept;
        [[nodiscard]] static IntSlots& PrimesKilled() noexcept;

        [[nodiscard]] static const ModeStateAction& ModeState() noexcept;

        static void PauseMenu();
        static void UnpauseMenu();
        static void PauseDialog() noexcept;
        static void UnpauseDialog() noexcept;
        static void ApplyPause();
        [[nodiscard]] static bool IsTeamMode(GameMode mode);
        static void Setup(Scene* scene);
        static void ResetMatchProgress() noexcept;
        static void UpdateTime(Scene* scene);
        static void ProcessFrame(Scene* scene);
        [[nodiscard]] static AreaState GetAreaState(
            std::int32_t areaId, StorySaveValue* save = nullptr);

        [[nodiscard]] static bool QueuedOublietteUnlockMessage() noexcept;
        static void QueuedOublietteUnlockMessage(bool value) noexcept;
        static void ModeStateAdventure(Scene* scene);
        static void ModeStateBattle(Scene* scene);
        static void ModeStateSurvival(Scene* scene);
        static void UpdateSurvival(float frameTime);

        // GameStateTeams.cpp: the C# partial's half of this class.
        [[nodiscard]] static bool IsResultTie();
        // ResultSlots groups tied teams deterministically; Standings preserves
        // the tie.
        static void UpdateStandings();
        static void ModeStateCapture(Scene* scene);
        static void ModeStateBounty(Scene* scene);
        static void ModeStateDefender(Scene* scene);
        static void ModeStateNodes(Scene* scene);
        static void ModeStatePrimeHunter(Scene* scene);

        [[nodiscard]] static std::int32_t QueuedOctolithMessageId() noexcept;
        static void QueuedOctolithMessageId(std::int32_t value) noexcept;
        static void UpdateFrame(Scene* scene);
        static void UpdateBossFlags(std::int32_t areaId);
        static void ResetEscapeState(bool updateSounds);
        static void UpdateState();
        static void CompleteRandomEncounter(std::int32_t roomId);

        static std::shared_ptr<StorySaveValue> StorySave;

        static void UpdateCleanSave(bool force);
        static void RestoreCleanSave();
        static void LoadSave();
        static void StartNewSave();
        [[nodiscard]] static std::shared_ptr<StorySaveValue> ReadSave();
        [[nodiscard]] static bool SaveExists(std::uint8_t slot);
        [[nodiscard]] static std::shared_ptr<StorySaveValue> PeekSave(std::uint8_t slot);
        static void CommitSave();
        [[nodiscard]] static std::shared_ptr<MenuSettings> LoadSettings();
        static void CommitSettings(const std::shared_ptr<MenuSettings>& menuSettings);
        static void Reset();

    public:
        class ByteArrayConverter final
        {
        public:
            [[nodiscard]] static StorySaveValue::ByteArray Read(
                const std::shared_ptr<ManagedArray<std::int16_t>>& values);
            [[nodiscard]] static std::vector<std::uint8_t> Write(
                const StorySaveValue::ByteArray& values);
        };

    private:
        class SerializedSettings final
        {
        public:
            std::shared_ptr<const std::unordered_map<std::string, std::string>> Features{};
            std::shared_ptr<MenuSettings> MenuSettingsValue{};
        };

        [[nodiscard]] static NicknameSlots BuildDefaultNicknames();
        static void EnsureIntroCamSeq();
        static void EndIfPointGoalReached();
        static void EnterShip();
        static void UpdateEscapeState(std::int32_t frames, std::int32_t stateId);
        static void UpdateEventSounds(float timer);
        [[nodiscard]] static std::int32_t ComparePlayers(
            std::int32_t slot1, std::int32_t slot2);
        [[nodiscard]] static std::int32_t CompareTeams(
            std::int32_t slot1, std::int32_t slot2);
        [[nodiscard]] static std::string GetSavePath(std::uint8_t slot);
        [[nodiscard]] static std::string GetSettingsPath();

        static GameMode _mode;
        static bool _pausePrevented;
        static bool _menuPause;
        static bool _dialogPause;
        static MatchStateValue _matchState;
        static TransitionStateValue _transitionState;
        static EscapeStateValue _escapeState;
        static float _escapeTimer;
        static bool _escapePaused;

        static IntSlots _encounterState;
        static BoolRooms _completedRandomEncounterRooms;
        static std::int32_t _transitionRoomId;
        static bool _transitionAltForm;
        static std::int32_t _activePlayers;
        static NicknameSlots _nicknames;
        static IntSlots _stars;
        static IntSlots _standings;
        static IntSlots _teamStandings;
        static IntSlots _resultSlots;
        static std::int32_t _primeHunter;

        static bool _teams;
        static std::int32_t _teamCount;
        static bool _friendlyFire;
        static std::int32_t _pointGoal;
        static float _timeGoal;
        static bool _octolithReset;
        static bool _radarPlayers;
        static bool _affinityWeapons;
        static bool _shadowFreeze;
        static bool _instagib;
        static float _matchTime;
        static bool _forceEndGame;

        static IntSlots _points;
        static IntSlots _teamPoints;
        static IntSlots _kills;
        static IntSlots _teamKills;
        static IntSlots _deaths;
        static IntSlots _teamDeaths;
        static FloatSlots _time;
        static FloatSlots _teamTime;
        static IntSlots _beamDamageMax;
        static IntSlots _beamDamageDealt;
        static IntSlots _damageCount;
        static IntSlots _altDamageCount;
        static IntSlots _killStreak;
        static IntSlots _suicides;
        static IntSlots _friendlyKills;
        static IntSlots _headshotKills;
        static BeamKillSlots _beamKills;
        static IntSlots _octolithScores;
        static IntSlots _octolithDrops;
        static IntSlots _octolithStops;
        static IntSlots _nodesCaptured;
        static IntSlots _nodesLost;
        static IntSlots _killsAsPrime;
        static IntSlots _primesKilled;

        static ModeStateAction _modeState;
        static bool _pausingDialog;
        static bool _unpausingDialog;

        static bool _tempoChanged;
        static bool _stateChanged;
        static float _matchEndTime;
        static float _lastAlarmTime;
        static std::int32_t _nextAlarmIndex;
        static const std::array<float, 4> _alarmIntervals;

        static bool _whiteoutStarted;
        static bool _gameOverShown;
        static std::int32_t _queuedOctolithMessageId;
        static bool _queuedOublietteUnlockMessage;
        static bool _playedTimedEventSfx;

        static std::shared_ptr<StorySaveValue> _cleanStorySave;
    };
}
