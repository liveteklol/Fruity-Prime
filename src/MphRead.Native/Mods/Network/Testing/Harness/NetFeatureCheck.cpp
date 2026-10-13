#include "Mods/Network/Testing/Harness/NetFeatureCheck.hpp"

#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Gameplay/Replication/FormReconciliation.hpp"
#include "Mods/Network/Gameplay/StatusEffects/TimedEffectSync.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Bombs/NetBombs.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Domain/PowerUps.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Testing/Harness/NetTestScript.hpp"
#include "Mods/SpectatorMode.hpp"
#include "Mods/WorldEvents.hpp"
#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/BombEntity.hpp"
#include "Entities/ItemInstanceEntity.hpp"
#include "Entities/Players/HalfturretEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"
#include "Scene.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Encoding.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"
#include "Formats/Types.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using ::MphRead::NativeRuntime::ConsoleWrite;
using ::MphRead::NativeRuntime::EnvironmentNewLine;
using ::MphRead::NativeRuntime::ConsoleWriteLine;
using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::Utf16Length;
using ::MphRead::NativeRuntime::WideToUtf8;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::Length;

namespace
{
    using MphRead::BeamType;
    using MphRead::Hunter;
    using MphRead::Entities::LoadFlags;
    using MphRead::Entities::PlayerFlags2;
    using MphRead::Mods::Network::PlayerState;
    using MphRead::Mods::Network::TestPhase;
    using OpenTK::Mathematics::Vector3;

    [[nodiscard]] bool AuthorityPlayerInPlay(const PlayerState* state) noexcept
    {
        constexpr std::uint8_t activeFlags = PlayerState::FlagActive | PlayerState::FlagSpawned;
        return state != nullptr && (state->Flags & activeFlags) == activeFlags && state->Health > 0;
    }

    [[nodiscard]] constexpr std::int32_t SubInt32(
        std::int32_t left, std::int32_t right) noexcept
    {
        return std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] double HeightRange(bool hasHeight, float minY, float maxY) noexcept
    {
        return hasHeight && maxY > minY ? static_cast<double>(maxY - minY) : 0.0;
    }

    void IncludeHeightSample(bool& hasHeight, float& minY, float& maxY, float sample) noexcept
    {
        if (!std::isfinite(sample))
        {
            return;
        }
        if (!hasHeight)
        {
            minY = sample;
            maxY = sample;
            hasHeight = true;
            return;
        }
        minY = std::min(minY, sample);
        maxY = std::max(maxY, sample);
    }

    [[nodiscard]] std::size_t ManagedArrayLength(std::int32_t length)
    {
        if (length < 0)
        {
            throw System::OverflowException();
        }
        return static_cast<std::size_t>(length);
    }

    [[nodiscard]] std::string PadLeftManaged(std::string value, std::size_t width)
    {
        const std::size_t length = Utf16Length(value);
        if (length < width)
        {
            value.insert(0, width - length, ' ');
        }
        return value;
    }

    [[nodiscard]] std::string PadRightManaged(std::string value, std::size_t width)
    {
        const std::size_t length = Utf16Length(value);
        if (length < width)
        {
            value.append(width - length, ' ');
        }
        return value;
    }

    [[nodiscard]] std::string TestPhaseName(TestPhase phase)
    {
        return ::MphRead::Mods::Network::ToString(phase);
    }

    [[nodiscard]] bool IsMorphPhase(TestPhase phase) noexcept
    {
        return phase == TestPhase::MorphA || phase == TestPhase::AltAttackA
            || phase == TestPhase::MorphB || phase == TestPhase::AltAttackB;
    }

    [[nodiscard]] bool IsUnmorphSamplePhase(TestPhase phase) noexcept
    {
        return phase == TestPhase::Zoom || phase == TestPhase::Duel;
    }

    [[nodiscard]] std::string Join(const std::vector<std::string>& values)
    {
        std::string result;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i != 0)
            {
                result += ", ";
            }
            result += values[i];
        }
        return result;
    }

    struct FormTraceState final
    {
        bool Initialized = false;
        bool InPlay = false;
        bool AltForm = false;
        bool Morphing = false;
        bool Unmorphing = false;
        std::uint8_t Progress = 0;

        [[nodiscard]] bool Changed(bool inPlay, bool altForm, bool morphing,
            bool unmorphing, std::uint8_t progress) const noexcept
        {
            return !Initialized || InPlay != inPlay || AltForm != altForm
                || Morphing != morphing || Unmorphing != unmorphing || Progress != progress;
        }

        [[nodiscard]] std::string Event(bool inPlay, bool altForm, bool morphing,
            bool unmorphing, std::uint8_t progress) const
        {
            if (!Initialized) return "baseline";
            if (InPlay != inPlay) return "in-play";
            if (AltForm != altForm) return "form-commit";
            const bool wasTransitioning = Morphing || Unmorphing;
            const bool transitioning = morphing || unmorphing;
            if (!wasTransitioning && transitioning) return "transition-begin";
            if (wasTransitioning && !transitioning) return "transition-end";
            if (Morphing != morphing || Unmorphing != unmorphing) return "transition-direction";
            if (Progress != progress) return "progress";
            return "state";
        }

        void Set(bool inPlay, bool altForm, bool morphing, bool unmorphing,
            std::uint8_t progress) noexcept
        {
            Initialized = true;
            InPlay = inPlay;
            AltForm = altForm;
            Morphing = morphing;
            Unmorphing = unmorphing;
            Progress = progress;
        }
    };

    void TraceFormState(FormTraceState& previous, const char* source, const std::string& role,
        std::int32_t slot, MphRead::Hunter hunter, std::uint32_t frame,
        std::optional<std::uint32_t> authorityFrame, bool inPlay, bool altForm,
        bool morphing, bool unmorphing, std::uint8_t progress)
    {
        if (!previous.Changed(inPlay, altForm, morphing, unmorphing, progress))
        {
            return;
        }
        const std::string event = previous.Event(inPlay, altForm, morphing, unmorphing, progress);
        std::string message = "[form-timeline] source=";
        message += source;
        message += " role=" + role
            + " event=" + event
            + " slot=" + std::to_string(slot)
            + " hunter=" + ::MphRead::ToString(hunter)
            + " frame=" + std::to_string(frame);
        if (authorityFrame.has_value())
        {
            message += " authorityFrame=" + std::to_string(*authorityFrame);
        }
        message += " inPlay=" + std::string(inPlay ? "true" : "false")
            + " alt=" + std::string(altForm ? "true" : "false")
            + " transition=" + (morphing ? std::string("morph")
                : unmorphing ? std::string("unmorph") : std::string("none"))
            + " progress=" + std::to_string(progress);
        MphRead::Mods::Network::NetLog::Event(message);
        previous.Set(inPlay, altForm, morphing, unmorphing, progress);
    }
}

namespace MphRead::Mods::Network
{
    class NetFeatureCheck::Record final
    {
    public:
        std::int32_t SpawnedFrames = 0;
        std::int32_t MovedFrames = 0;
        double Travelled = 0.0;
        float MinY = std::numeric_limits<float>::max();
        float MaxY = std::numeric_limits<float>::lowest();
        double FacingDegrees = 0.0;
        std::int32_t BeamFrames = 0;
        std::int32_t ShotsFired = 0;
        // Shot events: made, for my own player; fired from events, for a copy.
        double ShotEvents = 0;
        std::int32_t LastFiredTotal = 0;
        std::int32_t BombFrames = 0;
        // Bombs that appeared: each bomb entity counted the frame it is first
        // seen, so a bomb another machine never laid, or laid twice, shows.
        std::int32_t BombsLaid = 0;
        std::vector<const Entities::BombEntity*> LastBombs{};
        double BombPositionErrorTotal = 0.0;
        double WorstBombPositionError = 0.0;
        std::int32_t BombPositionSamples = 0;
        std::int32_t HalfturretFrames = 0;
        // The local owner's parity value is sampled from authority state;
        // remote records use the form actually applied to their replica.
        std::int32_t AltFormFrames = 0;
        std::int32_t AltFormInMorphPhase = 0;
        std::int32_t BipedInUnmorphPhase = 0;
        std::int32_t WeaponChanges = 0;
        std::int32_t AltAttackPresses = 0;
        bool HasAltAttackObservation = false;
        bool LastAltAttackActive = false;
        std::int32_t DamageEvents = 0;
        // Health that went up while alive: a pickup, a drain -- or damage
        // shown and then taken back, which is what it is here to count.
        std::int32_t HealthRises = 0;
        std::int32_t DamageInAltForm = 0;
        std::int32_t Deaths = 0;
        std::int32_t ZoomFrames = 0;
        bool HasZoomObservation = false;
        bool LastLocalZoomed = false;
        bool LastAuthorityZoomed = false;
        bool HasDoubleDamageObservation = false;
        bool LastLocalDoubleDamage = false;
        bool LastAuthorityDoubleDamage = false;
        std::int32_t FrozenFrames = 0;
        std::int32_t DisruptedFrames = 0;
        std::int32_t BurningFrames = 0;
        std::int32_t SpectatingFrames = 0;
        std::int32_t DoubleDamageFrames = 0;
        Vector3 LastPosition{};
        Vector3 LastFramePosition{};
        bool HaveFramePrevious = false;
        Vector3 LastFacing{};
        bool HavePrevious = false;
        std::int32_t LastHealth = -1;
        BeamType LastWeapon = BeamType::None;
        bool WasAlive = false;
        MphRead::Hunter Hunter{};
        std::int32_t FormDisagreeFrames = 0;
        std::int32_t FormDisagreeRun = 0;
        std::int32_t WorstFormDisagreeRun = 0;
        std::string WorstFormContext{};
        std::int32_t FormTransitionSamples = 0;
        std::int32_t FormTransitionDirectionMismatches = 0;
        bool FormTransitionDirectionMismatchActive = false;
        std::int32_t FormProgressErrorTotal = 0;
        std::int32_t FormProgressErrorMax = 0;
        FormTraceState SimulationFormTrace{};
        FormTraceState AuthorityFormTrace{};
        double WorstPositionGap = 0.0;
        double WorstStep = 0.0;
        std::int32_t Teleports = 0;
        bool EverCompared = false;
        std::int32_t FramesSinceRespawn = 0;
        std::int32_t FramesSinceLaunch = 0;
        std::int32_t LastWorldEvents = 0;
    };

    // Parity tolerances are measured, not chosen: two clients at 150 +-20 ms
    // and 5% loss (2026-10-10) agreed within 2% on every running count below,
    // once Weavel's copies stopped trailing a whole morph animation (protocol
    // 22) -- which is what this check found. Shooting and bombs count frames
    // a projectile or bomb is alive, which ends on whatever it meets -- and a
    // copy meets things a few frames apart from the original: 93-109% over
    // every run at 0-250 ms, hence 12%. Whether every shot was reproduced is
    // shot-events', exactly. At 25% loss the form and bomb counts drift past
    // these tolerances. A count of
    // discrete things (shots, hits) is exact but for the tail still in flight
    // when the window closed. Facing sums every degree turned, jitter
    // included, so it measures the copy's aim noise and is not compared.
    std::array<NetFeatureCheck::Feature, NetFeatureCheck::FeatureCount> NetFeatureCheck::_features{{
        {"spawn", [](const Record& r) -> double { return r.SpawnedFrames; }, 30, "frames", {0.01, 30}},
        {"movement", [](const Record& r) -> double { return r.Travelled; }, 5, "units", {0.03, 1}},
        {"jump", &NetFeatureCheck::Height, 1.5, "units", {0.05, 0.2}, false,
            nullptr, NetFeatureCheck::WindowMetric::HeightRange},
        {"facing", [](const Record& r) -> double { return r.FacingDegrees; }, 180, "deg"},
        {"shooting", [](const Record& r) -> double { return r.BeamFrames; }, 10, "beam-frames", {0.12, 10}},
        {"shots", [](const Record& r) -> double { return r.ShotsFired; }, 10, "shots", {0, 2}},
        {"shot-events", [](const Record& r) -> double { return r.ShotEvents; }, 10, "events", {0, 2}},
        {"weapon-switch", [](const Record& r) -> double { return r.WeaponChanges; }, 2, "changes", {}, true},
        {"alt-attack", [](const Record& r) -> double { return r.AltAttackPresses; }, 3, "presses", {}, true},
        // Counted inside the phases only, so a copy's later start is cut off
        // and its later finish is not: these measure the latency, which is
        // why parity is the total's.
        {"alt-form", [](const Record& r) -> double { return r.AltFormInMorphPhase; }, 30, "frames"},
        {"alt-form-total", [](const Record& r) -> double { return r.AltFormFrames; }, 30, "frames", {0.03, 15}},
        {"unmorph", [](const Record& r) -> double { return r.BipedInUnmorphPhase; }, 30, "frames"},
        {"bombs", [](const Record& r) -> double { return r.BombFrames; }, 5, "frames", {0.12, 15}, false,
            &NetFeatureCheck::LaysBombs},
        {"bombs-laid", [](const Record& r) -> double { return r.BombsLaid; }, 3, "bombs", {0, 2}, false,
            &NetFeatureCheck::LaysBombs},
        {"halfturret", [](const Record& r) -> double { return r.HalfturretFrames; }, 5, "frames", {0.03, 15}, false,
            &NetFeatureCheck::IsWeavel},
        {"zoom", [](const Record& r) -> double { return r.ZoomFrames; }, 10, "frames", {0.03, 10}},
        {"frozen", [](const Record& r) -> double { return r.FrozenFrames; }},
        {"disrupted", [](const Record& r) -> double { return r.DisruptedFrames; }},
        {"burning", [](const Record& r) -> double { return r.BurningFrames; }},
        {"spectating", [](const Record& r) -> double { return r.SpectatingFrames; }},
        {"double-damage", [](const Record& r) -> double { return r.DoubleDamageFrames; }, 10, "frames", {0.03, 10}},
        {"damage-taken", [](const Record& r) -> double { return r.DamageEvents; }, 1, "hits", {0, 1}},
        {"health-rises", [](const Record& r) -> double { return r.HealthRises; }},
        {"hit-in-alt-form", [](const Record& r) -> double { return r.DamageInAltForm; }, 2, "hits", {}, true},
        {"deaths", [](const Record& r) -> double { return r.Deaths; }, 1, "deaths", {}, true},
        {"teleports", [](const Record& r) -> double { return r.Teleports; }}
    }};

    NetFeatureCheck::NetFeatureCheck()
        : _records(ManagedArrayLength(Entities::PlayerEntity::MaxPlayers())),
          _pairs(ManagedArrayLength(Entities::PlayerEntity::MaxPlayers())),
          Boards(_boards)
    {
        for (std::unique_ptr<Record>& record : _records)
        {
            record = std::make_unique<Record>();
        }
    }

    NetFeatureCheck::~NetFeatureCheck() = default;

    NetFeatureCheck::Record& NetFeatureCheck::RecordAt(std::int32_t slot)
    {
        return *ManagedAt(_records, slot);
    }

    const NetFeatureCheck::Record& NetFeatureCheck::RecordAt(std::int32_t slot) const
    {
        return *ManagedAt(_records, slot);
    }

    void NetFeatureCheck::IncrementPhase(TestPhase phase)
    {
        for (auto& pair : _phaseFrames)
        {
            if (pair.first == phase)
            {
                pair.second = UncheckedAdd(pair.second, 1);
                return;
            }
        }
        _phaseFrames.emplace_back(phase, 1);
    }

    void NetFeatureCheck::Reset()
    {
        for (std::unique_ptr<Record>& record : _records)
        {
            record = std::make_unique<Record>();
        }
        std::fill(_pairs.begin(), _pairs.end(), PairWindow{});
        _freezeTimeline.Reset();
        _freezeMismatchLogged = {};
    }

    NetFeatureCheck::Values NetFeatureCheck::Read(const Record& record)
    {
        Values values{};
        for (std::size_t i = 0; i < _features.size(); ++i)
        {
            values[i] = _features[i].Get(record);
        }
        return values;
    }

    void NetFeatureCheck::TrackPairs()
    {
        const Values mine = Read(RecordAt(_localSlot));
        const auto& players = Entities::PlayerEntity::Players();
        const std::shared_ptr<Entities::PlayerEntity> owner
            = static_cast<std::size_t>(_localSlot) < players.size()
                ? ManagedAt(players, _localSlot) : nullptr;
        for (std::size_t slot = 0; slot < _pairs.size(); ++slot)
        {
            PairWindow& pair = _pairs[slot];
            const bool present = static_cast<std::int32_t>(slot) != _localSlot && slot < players.size()
                && players[slot] != nullptr && TestFlag(players[slot]->LoadFlags(), LoadFlags::Active);
            if (present && !pair.Present)
            {
                pair.OwnerStart = mine;
                pair.SeenStart = Read(RecordAt(static_cast<std::int32_t>(slot)));
                pair.OwnerHasHeight = false;
                pair.SeenHasHeight = false;
                pair.Opened = true;
            }

            if (present)
            {
                if (owner && owner->ModIsInPlay())
                {
                    IncludeHeightSample(pair.OwnerHasHeight, pair.OwnerMinY,
                        pair.OwnerMaxY, owner->Position.Y);
                }
                const auto& seen = ManagedAt(players, static_cast<std::int32_t>(slot));
                if (seen && seen->ModIsInPlay())
                {
                    IncludeHeightSample(pair.SeenHasHeight, pair.SeenMinY,
                        pair.SeenMaxY, seen->Position.Y);
                }
            }
            else if (pair.Present)
            {
                const Values seen = Read(RecordAt(static_cast<std::int32_t>(slot)));
                for (std::size_t i = 0; i < mine.size(); ++i)
                {
                    if (_features[i].Window == WindowMetric::HeightRange)
                    {
                        pair.OwnerTotal[i] += HeightRange(pair.OwnerHasHeight,
                            pair.OwnerMinY, pair.OwnerMaxY);
                        pair.SeenTotal[i] += HeightRange(pair.SeenHasHeight,
                            pair.SeenMinY, pair.SeenMaxY);
                    }
                    else
                    {
                        pair.OwnerTotal[i] += mine[i] - pair.OwnerStart[i];
                        pair.SeenTotal[i] += seen[i] - pair.SeenStart[i];
                    }
                }
                pair.OwnerHasHeight = false;
                pair.SeenHasHeight = false;
            }
            pair.Present = present;
        }
    }

    NetFeatureCheck::Values NetFeatureCheck::MineWith(std::int32_t slot) const
    {
        const PairWindow& pair = ManagedAt(_pairs, slot);
        Values values = pair.OwnerTotal;
        if (pair.Present)
        {
            const Values mine = Read(RecordAt(_localSlot));
            for (std::size_t i = 0; i < mine.size(); ++i)
            {
                values[i] += _features[i].Window == WindowMetric::HeightRange
                    ? HeightRange(pair.OwnerHasHeight, pair.OwnerMinY, pair.OwnerMaxY)
                    : mine[i] - pair.OwnerStart[i];
            }
        }
        return values;
    }

    NetFeatureCheck::Values NetFeatureCheck::SawWith(std::int32_t slot) const
    {
        const PairWindow& pair = ManagedAt(_pairs, slot);
        Values values = pair.SeenTotal;
        if (pair.Present)
        {
            const Values seen = Read(RecordAt(slot));
            for (std::size_t i = 0; i < seen.size(); ++i)
            {
                values[i] += _features[i].Window == WindowMetric::HeightRange
                    ? HeightRange(pair.SeenHasHeight, pair.SeenMinY, pair.SeenMaxY)
                    : seen[i] - pair.SeenStart[i];
            }
        }
        return values;
    }

    void NetFeatureCheck::Count(
        std::span<std::int32_t> counts, Entities::EntityBase* owner)
    {
        if (auto* player = dynamic_cast<Entities::PlayerEntity*>(owner);
            player != nullptr && player->SlotIndex() >= 0
            && player->SlotIndex() < static_cast<std::int32_t>(counts.size()))
        {
            const std::int32_t index = player->SlotIndex();
            std::int32_t& value = ManagedAt(counts, index);
            value = UncheckedAdd(value, 1);
        }
        else if (auto* turret = dynamic_cast<Entities::HalfturretEntity*>(owner);
            turret != nullptr)
        {
            const std::shared_ptr<Entities::PlayerEntity> firstOwner = turret->Owner();
            if (!firstOwner)
            {
                throw System::NullReferenceException();
            }
            if (firstOwner->SlotIndex() >= 0)
            {
                const std::shared_ptr<Entities::PlayerEntity> secondOwner = turret->Owner();
                if (!secondOwner)
                {
                    throw System::NullReferenceException();
                }
                if (secondOwner->SlotIndex() < static_cast<std::int32_t>(counts.size()))
                {
                    const std::shared_ptr<Entities::PlayerEntity> thirdOwner = turret->Owner();
                    if (!thirdOwner)
                    {
                        throw System::NullReferenceException();
                    }
                    const std::int32_t index = thirdOwner->SlotIndex();
                    std::int32_t& value = ManagedAt(counts, index);
                    value = UncheckedAdd(value, 1);
                }
            }
        }
    }

    void NetFeatureCheck::Observe(MphRead::Scene& scene)
    {
        Mods::WorldEvents::Watching(true);
        _localSlot = std::max(NetSession::LocalSlot(), 0);

        const bool localSamplePath
            = NetSession::NetFrame() % NetConfig::IntentSendInterval == 0;
        const TestPhase phase = NetTestScript::Phase();
        IncrementPhase(phase);

        const std::size_t beamCount = ManagedArrayLength(
            Entities::PlayerEntity::MaxPlayers());
        std::vector<std::int32_t> beams(beamCount, 0);
        const std::size_t bombCount = ManagedArrayLength(
            Entities::PlayerEntity::MaxPlayers());
        std::vector<std::int32_t> bombs(bombCount, 0);
        std::vector<std::vector<const Entities::BombEntity*>> bombEntities(bombCount);
        const std::size_t turretCount = ManagedArrayLength(
            Entities::PlayerEntity::MaxPlayers());
        std::vector<std::int32_t> turrets(turretCount, 0);

        {
            auto enumerator = scene.Entities().GetEnumerator();
            while (enumerator.MoveNext())
            {
                const std::shared_ptr<Entities::EntityBase> entity = enumerator.Current();
                if (!entity)
                {
                    throw System::NullReferenceException();
                }
                if (entity->Type == EntityType::BeamProjectile)
                {
                    const auto beam
                        = std::dynamic_pointer_cast<Entities::BeamProjectileEntity>(entity);
                    Count(beams, beam ? beam->Owner().get() : nullptr);
                }
                else if (entity->Type == EntityType::Bomb)
                {
                    const auto bomb
                        = std::dynamic_pointer_cast<Entities::BombEntity>(entity);
                    Entities::PlayerEntity* owner
                        = bomb ? dynamic_cast<Entities::PlayerEntity*>(bomb->Owner()) : nullptr;
                    Count(bombs, owner);
                    if (owner != nullptr && owner->SlotIndex() >= 0
                        && static_cast<std::size_t>(owner->SlotIndex()) < bombEntities.size())
                    {
                        bombEntities[static_cast<std::size_t>(owner->SlotIndex())].push_back(bomb.get());
                        const std::int32_t slot = owner->SlotIndex();
                        if (NetBombs::Drives(*owner) && bomb->ModSequence != 0)
                        {
                            const auto reported = NetBombs::ReportedPosition(slot, bomb->ModSequence);
                            if (reported.has_value())
                            {
                                const Vector3 copyPosition = static_cast<Vector3>(bomb->Position);
                                const double error = static_cast<double>(Length(*reported - copyPosition));
                                if (std::isfinite(error))
                                {
                                    Record& record = RecordAt(slot);
                                    record.BombPositionErrorTotal += error;
                                    record.WorstBombPositionError = MathMax(record.WorstBombPositionError, error);
                                    IncrementInPlace(record.BombPositionSamples);
                                }
                            }
                        }
                    }
                }
                else if (entity->Type == EntityType::Halfturret)
                {
                    const auto turret
                        = std::dynamic_pointer_cast<Entities::HalfturretEntity>(entity);
                    Count(turrets, turret ? turret->Owner().get() : nullptr);
                }
            }
        }

        _itemsNow = 0;
        {
            auto enumerator = scene.GetItemInstanceEntities().GetEnumerator();
            while (enumerator.MoveNext())
            {
                const std::shared_ptr<Entities::ItemInstanceEntity> item = enumerator.Current();
                static_cast<void>(item);
                IncrementInPlace(_itemsNow);
            }
        }
        if (_lastItemCount > _itemsNow)
        {
            _itemsPickedUp = UncheckedAdd(
                _itemsPickedUp, SubInt32(_lastItemCount, _itemsNow));
        }
        _lastItemCount = _itemsNow;
        IncrementInPlace(_itemSamples);
        _itemTotal = UncheckedAdd(_itemTotal, _itemsNow);

        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            if (static_cast<std::size_t>(slot) >= Entities::PlayerEntity::Players().size())
            {
                continue;
            }
            Entities::PlayerEntity& player = RequireReference(
                ManagedAt(Entities::PlayerEntity::Players(), slot));
            if (!TestFlag(player.LoadFlags(), LoadFlags::Active))
            {
                const bool hasAuthorityState = NetSession::HasRemoteState(slot);
                const PlayerState* authority = hasAuthorityState
                    ? &NetSession::RemoteState(static_cast<std::size_t>(slot)) : nullptr;
                const bool authorityInPlay = AuthorityPlayerInPlay(authority);
                _freezeTimeline.Observe(slot, false, 0, hasAuthorityState, authorityInPlay,
                    authority != nullptr ? authority->StatusEffects.FrozenFrames : 0,
                    static_cast<std::uint64_t>(NetSession::AppliedSnapshotFrame()),
                    NetSession::AppliedSnapshotFrame(), NetSession::NetFrame(),
                    NetPlayerLifecycle::Generation(slot));
                continue;
            }

            Record& record = RecordAt(slot);
            record.Hunter = player.Hunter();

            const bool spawned = TestFlag(player.LoadFlags(), LoadFlags::Spawned);
            const bool inPlay = player.ModIsInPlay();
            const bool hasAuthorityState = NetSession::HasRemoteState(slot);
            const PlayerState* authority = hasAuthorityState
                ? &NetSession::RemoteState(static_cast<std::size_t>(slot)) : nullptr;
            const bool authorityIsInPlay = AuthorityPlayerInPlay(authority);
            if (phase == TestPhase::FormCycle || phase == TestPhase::FormBombCycle)
            {
                const std::string role = NetSession::IsAuthority()
                    ? "authority" : slot == _localSlot ? "owner" : "replica";
                const bool morphing = player.IsMorphing();
                const bool unmorphing = player.IsUnmorphing();
                const std::int32_t localProgress = std::clamp(
                    static_cast<std::int32_t>(player.ModFormTransitionProgress()),
                    std::int32_t{0}, std::int32_t{15});
                TraceFormState(record.SimulationFormTrace, "simulation", role, slot,
                    player.Hunter(), NetSession::NetFrame(), std::nullopt, inPlay,
                    player.IsAltForm(), morphing, unmorphing,
                    static_cast<std::uint8_t>(localProgress));
                if (hasAuthorityState)
                {
                    const PlayerState& authority = NetSession::RemoteState(static_cast<std::size_t>(slot));
                    const bool authorityMorphing
                        = (authority.WeavelFlags & PlayerState::WeavelFlagMorphing) != 0;
                    const bool authorityUnmorphing
                        = (authority.WeavelFlags & PlayerState::WeavelFlagUnmorphing) != 0;
                    TraceFormState(record.AuthorityFormTrace, "authority-snapshot", role, slot,
                        player.Hunter(), NetSession::NetFrame(), NetSession::AppliedSnapshotFrame(),
                        (authority.Flags & PlayerState::FlagSpawned) != 0 && authority.Health > 0,
                        (authority.Flags & PlayerState::FlagAltForm) != 0,
                        authorityMorphing, authorityUnmorphing, authority.FormTransitionProgress());
                }
            }
            const std::uint32_t previousFreezeDelta
                = _freezeTimeline.At(static_cast<std::size_t>(slot)).AuthorityRemainingDeltaMax;
            const std::uint32_t snapshotAge = NetSession::SnapshotAge();
            const std::uint16_t authorityFrozenFrames = authority != nullptr
                ? authority->StatusEffects.FrozenFrames : 0;
            const std::uint16_t projectedAuthorityFrozenFrames
                = RemainingAfterSnapshotAge(authorityFrozenFrames, snapshotAge);
            _freezeTimeline.Observe(slot, inPlay, player.ModFrozenFrames(),
                hasAuthorityState, authorityIsInPlay,
                authorityFrozenFrames,
                static_cast<std::uint64_t>(NetSession::AppliedSnapshotFrame()),
                NetSession::AppliedSnapshotFrame(), NetSession::NetFrame(),
                NetPlayerLifecycle::Generation(slot), snapshotAge);
            const FreezeTimeline::SlotStats& freezeStats
                = _freezeTimeline.At(static_cast<std::size_t>(slot));
            constexpr std::uint32_t freezeTimerToleranceFrames = 4;
            if (authority != nullptr && !_freezeMismatchLogged[static_cast<std::size_t>(slot)]
                && freezeStats.AuthorityRemainingDeltaMax > freezeTimerToleranceFrames
                && freezeStats.AuthorityRemainingDeltaMax > previousFreezeDelta)
            {
                std::string message = "[freeze-timeline] timer-delta slot="
                    + std::to_string(slot)
                    + " delta=" + std::to_string(freezeStats.AuthorityRemainingDeltaMax)
                    + " local=" + std::to_string(player.ModFrozenFrames())
                    + " authority=" + std::to_string(authorityFrozenFrames)
                    + " projected=" + std::to_string(projectedAuthorityFrozenFrames)
                    + " local-generation=" + std::to_string(NetPlayerLifecycle::Generation(slot))
                    + " authority-generation=" + std::to_string(authority->SlotGeneration)
                    + " local-life=" + std::to_string(NetPlayerLifecycle::Get(slot))
                    + " authority-life=" + std::to_string(authority->LifeId)
                    + " local-frame=" + std::to_string(NetSession::NetFrame())
                    + " authority-frame=" + std::to_string(NetSession::AppliedSnapshotFrame())
                    + " snapshot-age=" + std::to_string(snapshotAge)
                    + " local-health=" + std::to_string(player.Health())
                    + " authority-health=" + std::to_string(authority->Health)
                    + " local-in-play=" + (inPlay ? "true" : "false")
                    + " authority-in-play=" + (authorityIsInPlay ? "true" : "false");
                NetLog::Event(message);
                _freezeMismatchLogged[static_cast<std::size_t>(slot)] = true;
            }

            if (ManagedAt(beams, slot) > 0)
            {
                IncrementInPlace(record.BeamFrames);
            }

            const std::int32_t firedTotal
                = ManagedAt(NetDamageMetrics::Read().Fired, slot);
            if (firedTotal > record.LastFiredTotal)
            {
                record.ShotsFired = UncheckedAdd(
                    record.ShotsFired, SubInt32(firedTotal, record.LastFiredTotal));
            }
            record.LastFiredTotal = firedTotal;
            record.ShotEvents = slot == _localSlot
                ? static_cast<double>(NetShotEvents::Sent())
                : static_cast<double>(NetShotEvents::Stats(slot).Fired);

            if (ManagedAt(bombs, slot) > 0)
            {
                IncrementInPlace(record.BombFrames);
            }
            {
                const auto& now = bombEntities[static_cast<std::size_t>(slot)];
                for (const Entities::BombEntity* bomb : now)
                {
                    if (std::find(record.LastBombs.begin(), record.LastBombs.end(), bomb) == record.LastBombs.end())
                    {
                        IncrementInPlace(record.BombsLaid);
                    }
                }
                record.LastBombs = now;
            }
            const bool altAttackPressed = player.Controls().AltAttack().IsPressed();
            const bool altAttackActive = TestFlag(player.Flags2(), PlayerFlags2::AltAttack);
            if (altAttackPressed)
            {
                IncrementInPlace(record.AltAttackPresses);
            }
            if (NetLog::Enabled()
                && (altAttackPressed
                    || (record.HasAltAttackObservation && altAttackActive != record.LastAltAttackActive)))
            {
                NetLog::Event("[altattack-observed] slot=" + std::to_string(slot)
                    + " role=" + (slot == _localSlot ? std::string("owner") : std::string("replica"))
                    + " phase=" + TestPhaseName(phase)
                    + " press=" + (altAttackPressed ? std::string("true") : std::string("false"))
                    + " held=" + (player.Controls().AltAttack().IsDown() ? std::string("true") : std::string("false"))
                    + " active=" + (altAttackActive ? std::string("true") : std::string("false"))
                    + " form=" + player.ModFormState()
                    + " frame=" + std::to_string(NetSession::NetFrame())
                    + " snapshot=" + std::to_string(NetSession::AppliedSnapshotFrame()));
            }
            record.HasAltAttackObservation = true;
            record.LastAltAttackActive = altAttackActive;
            if (ManagedAt(turrets, slot) > 0)
            {
                IncrementInPlace(record.HalfturretFrames);
            }

            // PlayerStateSnapshotCapture publishes Spawned from ModIsInPlay,
            // which also requires positive health. Keep parity counters on
            // the same alive interval; LoadFlags::Spawned can persist through
            // the death animation and count a corpse's stale form as active.
            if (!inPlay)
            {
                continue;
            }

            IncrementInPlace(record.SpawnedFrames);
            bool altFormForParity = player.IsAltForm();
            if (slot == _localSlot && hasAuthorityState)
            {
                const PlayerState& authority = NetSession::RemoteState(static_cast<std::size_t>(slot));
                const bool authorityInPlay
                    = (authority.Flags & PlayerState::FlagSpawned) != 0 && authority.Health > 0;
                altFormForParity = authorityInPlay
                    && (authority.Flags & PlayerState::FlagAltForm) != 0;
            }
            if (altFormForParity)
            {
                IncrementInPlace(record.AltFormFrames);
                if (IsMorphPhase(phase))
                {
                    IncrementInPlace(record.AltFormInMorphPhase);
                }
            }
            else if (IsUnmorphSamplePhase(phase))
            {
                IncrementInPlace(record.BipedInUnmorphPhase);
            }

            const std::shared_ptr<EquipInfo> equipInfo = player.EquipInfo();
            if (!equipInfo)
            {
                throw System::NullReferenceException();
            }
            const bool localZoomed = equipInfo->Zoomed;
            bool authorityInPlay = false;
            bool authorityZoomed = localZoomed;
            if (hasAuthorityState)
            {
                const PlayerState& authority = NetSession::RemoteState(static_cast<std::size_t>(slot));
                authorityInPlay = (authority.Flags & PlayerState::FlagSpawned) != 0 && authority.Health > 0;
                authorityZoomed = authorityInPlay && (authority.Flags & PlayerState::FlagZoomed) != 0;
            }
            // The local owner predicts the zoom toggle immediately, while
            // every observer measures the authority's replicated state. Use
            // that same authority value for the owner's parity counter.
            const bool zoomedForParity = slot == _localSlot && hasAuthorityState
                ? authorityZoomed : localZoomed;
            if (zoomedForParity)
            {
                IncrementInPlace(record.ZoomFrames);
            }
            if (NetLog::Enabled()
                && (!record.HasZoomObservation
                    || localZoomed != record.LastLocalZoomed
                    || (hasAuthorityState && authorityZoomed != record.LastAuthorityZoomed)))
            {
                NetLog::Event("[zoom-state] slot=" + std::to_string(slot)
                    + " role=" + (slot == _localSlot ? std::string("owner") : std::string("replica"))
                    + " phase=" + TestPhaseName(phase)
                    + " local=" + (localZoomed ? std::string("true") : std::string("false"))
                    + " authority=" + (hasAuthorityState
                        ? authorityZoomed ? std::string("true") : std::string("false")
                        : std::string("unavailable"))
                    + " authorityInPlay=" + (authorityInPlay ? std::string("true") : std::string("false"))
                    + " parity=" + (zoomedForParity ? std::string("true") : std::string("false"))
                    + " frame=" + std::to_string(NetSession::NetFrame())
                    + " snapshot=" + std::to_string(NetSession::AppliedSnapshotFrame())
                    + " weapon=" + std::to_string(static_cast<std::int32_t>(player.CurrentWeapon()))
                    + " canZoom=" + (player.ModCanZoom() ? std::string("true") : std::string("false"))
                    + " inputDown=" + (player.Controls().Zoom().IsDown() ? std::string("true") : std::string("false"))
                    + " inputPressed=" + (player.Controls().Zoom().IsPressed() ? std::string("true") : std::string("false")));
            }
            record.HasZoomObservation = true;
            record.LastLocalZoomed = localZoomed;
            record.LastAuthorityZoomed = authorityZoomed;
            if (player.ModFrozen())
            {
                IncrementInPlace(record.FrozenFrames);
            }
            if (player.ModDisrupted())
            {
                IncrementInPlace(record.DisruptedFrames);
            }
            if (player.ModBurning())
            {
                IncrementInPlace(record.BurningFrames);
            }
            if (slot == _localSlot
                    ? SpectatorMode::IsSpectating()
                    : TestFlag(player.Flags2(), PlayerFlags2::Spectating))
            {
                IncrementInPlace(record.SpectatingFrames);
            }
            const bool localDoubleDamage = player.DoubleDamage();
            bool authorityDoubleDamage = localDoubleDamage;
            if (hasAuthorityState)
            {
                const PlayerState& authority = NetSession::RemoteState(static_cast<std::size_t>(slot));
                const bool authorityInPlay = (authority.Flags & PlayerState::FlagSpawned) != 0
                    && authority.Health > 0;
                authorityDoubleDamage = authorityInPlay
                    && authority.PowerUps[IndexOf(PowerUp::DoubleDamage)].Remaining > 0;
            }
            // The owner's pickup is predicted locally, while other clients
            // count the applied authority report. Compare the same authority
            // state for the owner so an unconfirmed local prediction does not
            // masquerade as network presentation drift.
            const bool doubleDamageForParity = slot == _localSlot && hasAuthorityState
                ? authorityDoubleDamage : localDoubleDamage;
            if (doubleDamageForParity)
            {
                IncrementInPlace(record.DoubleDamageFrames);
            }
            if (NetLog::Enabled()
                && slot == _localSlot && hasAuthorityState
                && (!record.HasDoubleDamageObservation
                    || localDoubleDamage != record.LastLocalDoubleDamage
                    || authorityDoubleDamage != record.LastAuthorityDoubleDamage))
            {
                NetLog::Event("[double-damage-state] local="
                    + (localDoubleDamage ? std::string("true") : std::string("false"))
                    + " authority="
                    + (authorityDoubleDamage ? std::string("true") : std::string("false"))
                    + " parity="
                    + (doubleDamageForParity ? std::string("true") : std::string("false"))
                    + " authorityRemaining=" + std::to_string(
                        NetSession::RemoteState(static_cast<std::size_t>(slot))
                            .PowerUps[IndexOf(PowerUp::DoubleDamage)].Remaining)
                    + " frame=" + std::to_string(NetSession::NetFrame())
                    + " snapshot=" + std::to_string(NetSession::AppliedSnapshotFrame()));
            }
            record.HasDoubleDamageObservation = true;
            record.LastLocalDoubleDamage = localDoubleDamage;
            record.LastAuthorityDoubleDamage = authorityDoubleDamage;
            if (player.CurrentWeapon() != record.LastWeapon)
            {
                if (record.LastWeapon != BeamType::None)
                {
                    IncrementInPlace(record.WeaponChanges);
                }
                record.LastWeapon = player.CurrentWeapon();
            }
            if (record.LastHealth > 0 && player.Health() > record.LastHealth)
            {
                IncrementInPlace(record.HealthRises);
            }
            if (record.LastHealth > 0 && player.Health() > 0
                && player.Health() < record.LastHealth)
            {
                IncrementInPlace(record.DamageEvents);
                if (player.IsAltForm())
                {
                    IncrementInPlace(record.DamageInAltForm);
                }
            }
            if (record.WasAlive && player.Health() == 0)
            {
                IncrementInPlace(record.Deaths);
            }

            const std::int32_t jumpPads = Mods::WorldEvents::JumpPadsFor(slot);
            const std::int32_t teleports = Mods::WorldEvents::TeleportsFor(slot);
            const std::int32_t worldEvents = UncheckedAdd(jumpPads, teleports);
            record.FramesSinceLaunch = worldEvents != record.LastWorldEvents
                ? 0
                : UncheckedAdd(record.FramesSinceLaunch, 1);
            record.LastWorldEvents = worldEvents;
            record.FramesSinceRespawn
                = player.Health() > record.LastHealth || player.Health() == 0
                ? 0
                : UncheckedAdd(record.FramesSinceRespawn, 1);
            record.LastHealth = player.Health();
            record.WasAlive = player.Health() > 0;

            const float minYCurrent = record.MinY;
            const float minYPosition = player.Position.Y;
            record.MinY = MathMin(minYCurrent, minYPosition);
            const float maxYCurrent = record.MaxY;
            const float maxYPosition = player.Position.Y;
            record.MaxY = MathMax(maxYCurrent, maxYPosition);

            if (record.HaveFramePrevious)
            {
                const Vector3 framePosition = static_cast<Vector3>(player.Position);
                const float frameStep = Length(framePosition - record.LastFramePosition);
                if (frameStep > 0.01F)
                {
                    IncrementInPlace(record.MovedFrames);
                }
                if (frameStep > TeleportStep && record.FramesSinceRespawn > 60
                    && record.FramesSinceLaunch > LaunchGraceFrames)
                {
                    IncrementInPlace(record.Teleports);
                    const double worstStepCurrent = record.WorstStep;
                    record.WorstStep
                        = MathMax(worstStepCurrent, static_cast<double>(frameStep));
                }
            }
            record.LastFramePosition = static_cast<Vector3>(player.Position);
            record.HaveFramePrevious = true;

            const bool samplePath = slot != _localSlot || localSamplePath;
            if (samplePath && record.HavePrevious)
            {
                const Vector3 pathPosition = static_cast<Vector3>(player.Position);
                const float step = Length(pathPosition - record.LastPosition);
                if (step < 5.0F)
                {
                    record.Travelled += step;
                }
                const Vector3 gunVector = player.ModGunVector();
                const float rawDot = Vector3::Dot(gunVector, record.LastFacing);
                const float dot = std::clamp(rawDot, -1.0F, 1.0F);
                const float radians = std::acos(dot);
                constexpr float RadToDeg = 180.0F / 3.1415927F;
                const float degrees = radians * RadToDeg;
                record.FacingDegrees += degrees;
            }
            if (samplePath)
            {
                record.LastPosition = static_cast<Vector3>(player.Position);
                record.LastFacing = player.ModGunVector();
                record.HavePrevious = true;
            }

            if (slot == _localSlot
                || !NetSession::HasRemoteState(slot))
            {
                continue;
            }
            if (NetSession::IsAuthority())
            {
                continue;
            }

            record.EverCompared = true;
            const PlayerState state = NetSession::RemoteState(static_cast<std::size_t>(slot));
            ReplicatedFormState presentationForm{};
            const bool hasPresentationForm = NetSmoothing::SampleForm(slot, presentationForm);
            const bool wantAlt = hasPresentationForm
                ? presentationForm.AltForm
                : (state.Flags & PlayerState::FlagAltForm) != 0;
            const bool presentationMorphing = hasPresentationForm
                ? presentationForm.Direction == ReplicatedFormState::Transition::Morph
                : (state.WeavelFlags & PlayerState::WeavelFlagMorphing) != 0;
            const bool presentationUnmorphing = hasPresentationForm
                ? presentationForm.Direction == ReplicatedFormState::Transition::Unmorph
                : (state.WeavelFlags & PlayerState::WeavelFlagUnmorphing) != 0;
            const std::uint8_t presentationProgress = hasPresentationForm
                ? presentationForm.Progress : state.FormTransitionProgress();
            // Weavel's latest-snapshot fallback is not phase-locked. Measure
            // its native clip only when the shared presentation sample owns it.
            if (player.Hunter() != Hunter::Weavel || hasPresentationForm)
            {
                const bool presentationTransitionActive = presentationMorphing || presentationUnmorphing;
                const bool localTransitionMatches = presentationMorphing
                    ? player.IsMorphing() : presentationUnmorphing && player.IsUnmorphing();
                const bool directionMismatch = presentationTransitionActive && !localTransitionMatches;
                if (directionMismatch != record.FormTransitionDirectionMismatchActive)
                {
                    std::string event = "[form-animation] direction-mismatch ";
                    event += directionMismatch ? "begin" : "end";
                    event += " source=remote-copy slot=" + std::to_string(slot)
                        + " localSlot=" + std::to_string(_localSlot)
                        + " frame=" + std::to_string(NetSession::NetFrame())
                        + " snapshot=" + std::to_string(NetSession::AppliedSnapshotFrame())
                        + " presentationTransition=" + (presentationMorphing ? std::string("morph")
                            : presentationUnmorphing ? std::string("unmorph") : std::string("none"))
                        + " presentationProgress=" + std::to_string(presentationProgress)
                        + " presentationAlt=" + (wantAlt ? std::string("true") : std::string("false"))
                        + " localMorph=" + (player.IsMorphing() ? std::string("true") : std::string("false"))
                        + " localUnmorph=" + (player.IsUnmorphing() ? std::string("true") : std::string("false"))
                        + " localProgress=" + std::to_string(player.ModFormTransitionProgress())
                        + " local=" + player.ModFormState();
                    NetLog::Event(event);
                }
                record.FormTransitionDirectionMismatchActive = directionMismatch;
                if (directionMismatch)
                {
                    IncrementInPlace(record.FormTransitionDirectionMismatches);
                }
                if (presentationTransitionActive)
                {
                    IncrementInPlace(record.FormTransitionSamples);
                    const std::int32_t authorityProgress = presentationProgress;
                    const std::int32_t localProgress = player.ModFormTransitionProgress();
                    const std::int32_t progressError = std::abs(localProgress - authorityProgress);
                    record.FormProgressErrorTotal = UncheckedAdd(
                        record.FormProgressErrorTotal, progressError);
                    record.FormProgressErrorMax = MathMax(record.FormProgressErrorMax, progressError);
                }
            }
            const bool visible = player.Health() > 0
                && (state.Flags & PlayerState::FlagSpawned) != 0;
            if (visible && wantAlt != player.IsAltForm())
            {
                if (record.FormDisagreeRun == 0)
                {
                    NetLog::Event("[form-mismatch] begin source=remote-state slot="
                        + std::to_string(slot) + " localSlot=" + std::to_string(_localSlot)
                        + " frame=" + std::to_string(NetSession::NetFrame())
                        + " snapshot=" + std::to_string(NetSession::AppliedSnapshotFrame())
                        + " wanted=" + (wantAlt ? std::string("alt") : std::string("biped"))
                        + " presentationTransition=" + (presentationMorphing ? std::string("morph")
                            : presentationUnmorphing ? std::string("unmorph") : std::string("none"))
                        + " presentationProgress=" + std::to_string(presentationProgress)
                        + " local=" + player.ModFormState());
                }
                IncrementInPlace(record.FormDisagreeFrames);
                IncrementInPlace(record.FormDisagreeRun);
                if (record.FormDisagreeRun > record.WorstFormDisagreeRun)
                {
                    record.WorstFormDisagreeRun = record.FormDisagreeRun;
                    std::string context = "phase ";
                    context += TestPhaseName(phase);
                    context += ", authority wanted ";
                    context += wantAlt ? "alt" : "biped";
                    context += ", puppet ";
                    const std::string formState = player.ModFormState();
                    context += formState;
                    context += ", hp ";
                    const std::int32_t health = player.Health();
                    context += ::MphRead::NativeRuntime::ToString(health);
                    record.WorstFormContext = std::move(context);
                }
            }
            else
            {
                if (record.FormDisagreeRun > 0)
                {
                    NetLog::Event("[form-mismatch] end source=remote-state slot="
                        + std::to_string(slot) + " localSlot=" + std::to_string(_localSlot)
                        + " frame=" + std::to_string(NetSession::NetFrame())
                        + " snapshot=" + std::to_string(NetSession::AppliedSnapshotFrame())
                        + " duration=" + std::to_string(record.FormDisagreeRun)
                        + " wanted=" + (wantAlt ? std::string("alt") : std::string("biped"))
                        + " local=" + player.ModFormState());
                }
                record.FormDisagreeRun = 0;
            }
            if (!visible)
            {
                continue;
            }
            if ((state.Flags & PlayerState::FlagSpawned) != 0)
            {
                const Vector3 authorityPosition = state.Position;
                const Vector3 puppetPosition = static_cast<Vector3>(player.Position);
                const double gap = static_cast<double>(
                    Length(authorityPosition - puppetPosition));
                const double worstPositionGapCurrent = record.WorstPositionGap;
                record.WorstPositionGap = MathMax(worstPositionGapCurrent, gap);
            }
        }
        TrackPairs();
    }

    bool NetFeatureCheck::Report(std::int32_t& failures)
    {
        failures = 0;
        Record& mine = RecordAt(_localSlot);
        const std::string me
            = ManagedAt(GameState::Nicknames(), _localSlot);

        ConsoleWriteLine();
        ConsoleWriteLine(
            "  feature coverage (mine = what my player did, "
            "theirs = what I saw of them)");

        std::int32_t fails = 0;
        bool anyRemote = false;

        auto emit = [&me](
            std::string_view kind, const std::string& subject,
            std::string_view feature, double value)
        {
            std::string text = "  netcheck ";
            text += me;
            text += ' ';
            text += kind;
            text += ' ';
            text += subject;
            text += ' ';
            text += feature;
            text += ' ';
            text += ::MphRead::NativeRuntime::ToString(value, "0.##");
            ConsoleWriteLine(text);
        };

        // For tools/netcheck/compare-reports.py: who I am, and what counts
        // as each feature having happened.
        ConsoleWriteLine("  netcheck-hunter " + me + " " + ::MphRead::ToString(mine.Hunter));
        for (const Feature& feature : _features)
        {
            if (feature.Needed > 0)
            {
                ConsoleWriteLine("  netcheck-needed " + feature.Name + " "
                    + ::MphRead::NativeRuntime::ToString(feature.Needed, "0.##") + " "
                    + (feature.Pairwise ? "pairwise" : "single"));
            }
            if (feature.Agreement.Fraction >= 0)
            {
                ConsoleWriteLine("  netcheck-parity " + feature.Name + " "
                    + ::MphRead::NativeRuntime::ToString(feature.Agreement.Fraction, "0.###") + " "
                    + ::MphRead::NativeRuntime::ToString(feature.Agreement.Absolute, "0.##"));
            }
            if (feature.Applies != nullptr && !feature.Applies(mine.Hunter))
            {
                ConsoleWriteLine("  netcheck-na " + me + " " + feature.Name);
            }
        }
        for (const Feature& feature : _features)
        {
            emit("mine", me, feature.Name, feature.Get(mine));
        }

        for (std::size_t slot = 0; slot < FreezeTimeline::SlotCapacity; ++slot)
        {
            if (!_freezeTimeline.HasObservations(slot))
            {
                continue;
            }
            const FreezeTimeline::SlotStats& stats = _freezeTimeline.At(slot);
            const std::string subject = slot == static_cast<std::size_t>(_localSlot)
                ? "mine" : ManagedAt(GameState::Nicknames(), static_cast<std::int32_t>(slot));
            ConsoleWriteLine("  netcheck-freeze " + me + " " + subject
                + " slot=" + std::to_string(slot)
                + " applied-frames=" + std::to_string(stats.AppliedFrozenFrames)
                + " applied-episodes=" + std::to_string(stats.AppliedEpisodesStarted)
                + " applied-complete=" + std::to_string(stats.AppliedEpisodesCompleted)
                + " applied-min=" + std::to_string(stats.AppliedDurationMin)
                + " applied-max=" + std::to_string(stats.AppliedDurationMax)
                + " applied-local-complete=" + std::to_string(stats.AppliedLocalEpisodesCompleted)
                + " applied-local-min=" + std::to_string(stats.AppliedLocalDurationMin)
                + " applied-local-max=" + std::to_string(stats.AppliedLocalDurationMax)
                + " applied-remaining=" + std::to_string(stats.AppliedLastRemaining)
                + " applied-start-frame=" + std::to_string(stats.AppliedEpisodeStartFrame)
                + " applied-thaw-frame=" + std::to_string(stats.AppliedLastThawFrame)
                + " applied-authority-start-frame=" + std::to_string(stats.AppliedAuthorityEpisodeStartFrame)
                + " applied-authority-thaw-frame=" + std::to_string(stats.AppliedAuthorityLastThawFrame)
                + " applied-open=" + std::to_string(stats.AppliedEpisodeOpenFrames)
                + " applied-interrupted=" + std::to_string(stats.AppliedEpisodesInterrupted)
                + " authority-snapshots=" + std::to_string(stats.AuthoritySnapshots)
                + " authority-episodes=" + std::to_string(stats.AuthorityEpisodesStarted)
                + " authority-thaws=" + std::to_string(stats.AuthorityThaws)
                + " authority-min=" + std::to_string(stats.AuthorityDurationMin)
                + " authority-max=" + std::to_string(stats.AuthorityDurationMax)
                + " authority-frozen-samples=" + std::to_string(stats.AuthorityFrozenSamples)
                + " authority-remaining-delta-max=" + std::to_string(stats.AuthorityRemainingDeltaMax)
                + " authority-frozen-excluded=" + std::to_string(stats.AuthorityFrozenExcludedSamples)
                + " authority-remaining-delta-max-excluded="
                    + std::to_string(stats.AuthorityRemainingDeltaMaxExcluded)
                + " authority-start-frame=" + std::to_string(stats.AuthorityEpisodeStartFrame)
                + " authority-thaw-frame=" + std::to_string(stats.AuthorityLastThawFrame)
                + " authority-interrupted=" + std::to_string(stats.AuthorityEpisodesInterrupted)
                + " authority-open=" + std::to_string(stats.AuthorityEpisodeOpen)
                + " generation-max=" + std::to_string(stats.HighestOccupantGeneration)
                + " occupant-changes=" + std::to_string(stats.OccupantChanges)
                + " authority-frame=" + std::to_string(stats.AuthorityLastFrame)
                + " authority-remaining=" + std::to_string(stats.AuthorityLastRemaining));
        }

        for (std::int32_t slot = 0;
            static_cast<std::size_t>(slot) < _records.size(); ++slot)
        {
            if (slot == _localSlot || RecordAt(slot).SpawnedFrames == 0)
            {
                continue;
            }
            anyRemote = true;
            Record& other = RecordAt(slot);
            const std::string them
                = ManagedAt(GameState::Nicknames(), slot);
            std::string heading = "    --- as I saw ";
            heading += them;
            heading += " (slot ";
            heading += ::MphRead::NativeRuntime::ToString(slot);
            heading += ", ";
            heading += ::MphRead::ToString(other.Hunter);
            heading += ") ---";
            ConsoleWriteLine(heading);
            for (const Feature& feature : _features)
            {
                emit("saw", them, feature.Name, feature.Get(other));
            }
            if (other.BombPositionSamples > 0)
            {
                const double average = other.BombPositionErrorTotal / other.BombPositionSamples;
                ConsoleWriteLine("  netcheck-bomb-position " + me + " " + them
                    + " samples=" + std::to_string(other.BombPositionSamples)
                    + " mean=" + ::MphRead::NativeRuntime::ToString(average, "0.###")
                    + " max=" + ::MphRead::NativeRuntime::ToString(other.WorstBombPositionError, "0.###"));
            }
            if (other.FormTransitionSamples > 0)
            {
                const double meanProgressError
                    = static_cast<double>(other.FormProgressErrorTotal)
                    / static_cast<double>(other.FormTransitionSamples);
                ConsoleWriteLine("  netcheck-form-animation " + me + " " + them
                    + " samples=" + std::to_string(other.FormTransitionSamples)
                    + " direction-mismatches=" + std::to_string(other.FormTransitionDirectionMismatches)
                    + " mean-progress-error=" + ::MphRead::NativeRuntime::ToString(meanProgressError, "0.##")
                    + " max-progress-error=" + std::to_string(other.FormProgressErrorMax));
            }
            // What I did while they were here: what their "saw" of me is
            // compared with.
            if (ManagedAt(_pairs, slot).Opened)
            {
                const Values with = MineWith(slot);
                const Values seenWith = SawWith(slot);
                for (std::size_t i = 0; i < _features.size(); ++i)
                {
                    emit("mine-with", them, _features[i].Name, with[i]);
                    emit("saw-with", them, _features[i].Name, seenWith[i]);
                }
            }
            fails = UncheckedAdd(fails, ReportOne(mine, other, them));
        }

        if (!anyRemote)
        {
            ConsoleWriteLine(
                "    no other player was ever spawned in this scene -- nothing to compare");
            failures = 1;
            return false;
        }

        std::vector<std::string> invulnerable;
        for (std::int32_t slot = 0;
            slot < Entities::PlayerEntity::SlotCapacity
                && static_cast<std::size_t>(slot) < Entities::PlayerEntity::Players().size();
            ++slot)
        {
            const std::shared_ptr<Entities::PlayerEntity> player
                = ManagedAt(Entities::PlayerEntity::Players(), slot);
            if (RecordAt(slot).SpawnedFrames > 0)
            {
                if (!player)
                {
                    throw System::NullReferenceException();
                }
                if (TestFlag(player->LoadFlags(), LoadFlags::Spawned)
                    && !player->ModCanBeHurt())
                {
                    const std::string nickname
                        = ManagedAt(GameState::Nicknames(), slot);
                    invulnerable.push_back(
                        nickname + " (slot " + ::MphRead::NativeRuntime::ToString(slot) + ")");
                }
            }
        }
        if (!invulnerable.empty())
        {
            const std::string text
                = "    FAIL: no beam can hurt these players at all: " + Join(invulnerable);
            ConsoleWriteLine(text);
            IncrementInPlace(fails);
        }

        std::vector<std::string> untouched;
        for (std::int32_t slot = 0;
            static_cast<std::size_t>(slot) < _records.size(); ++slot)
        {
            if (RecordAt(slot).SpawnedFrames > 600
                && RecordAt(slot).DamageEvents == 0)
            {
                const std::string nickname
                    = ManagedAt(GameState::Nicknames(), slot);
                untouched.push_back(
                    nickname + " (slot " + ::MphRead::NativeRuntime::ToString(slot) + ")");
            }
        }
        if (!untouched.empty())
        {
            const std::string text
                = "    FAIL: never took a single hit: " + Join(untouched);
            ConsoleWriteLine(text);
            IncrementInPlace(fails);
        }

        if (_localSlot < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size()))
        {
            Entities::PlayerEntity& player = RequireReference(
                ManagedAt(Entities::PlayerEntity::Players(), _localSlot));
            const auto [rows, height] = player.ModScoreboardSize();
            const bool fits = height <= 192.0F;
            std::string text = "    scoreboard: ";
            text += ::MphRead::NativeRuntime::ToString(rows);
            text += " row(s), ";
            text += ::MphRead::NativeRuntime::ToString(height, "0");
            text += " px tall ";
            text += fits ? "(fits)" : "(OVERFLOWS the screen)";
            ConsoleWriteLine(text);
            if (!fits)
            {
                IncrementInPlace(fails);
            }
        }

        const std::int32_t itemsNow = _itemsNow;
        double itemAverage = 0.0;
        const std::int32_t itemSamplesCondition = _itemSamples;
        if (itemSamplesCondition > 0)
        {
            const std::int64_t itemTotalForAverage = _itemTotal;
            const std::int32_t itemSamplesDivisor = _itemSamples;
            itemAverage = static_cast<double>(itemTotalForAverage)
                / static_cast<double>(itemSamplesDivisor);
        }
        const std::int32_t itemsPickedUp = _itemsPickedUp;
        std::string items = "    items: ";
        items += ::MphRead::NativeRuntime::ToString(itemsNow);
        items += " on the map now, ";
        items += ::MphRead::NativeRuntime::ToString(itemAverage, "0.0");
        items += " on average, ";
        items += ::MphRead::NativeRuntime::ToString(itemsPickedUp);
        items += " taken or expired";
        ConsoleWriteLine(items);

        const std::string currentBoard = Scoreboard("    scoreboard as I see it:");
        ConsoleWriteLine(currentBoard);
        for (const auto& pair : Boards)
        {
            ConsoleWriteLine(pair.second);
        }
        if (!Boards.empty())
        {
        }

        std::string phases = "    phases seen:";
        for (const auto& pair : _phaseFrames)
        {
            std::string entry = " ";
            entry += TestPhaseName(pair.first);
            entry += '=';
            entry += ::MphRead::NativeRuntime::ToString(pair.second);
            phases += entry;
        }
        ConsoleWriteLine(phases);

        std::string pipeline = "    damage pipeline (resolved here / replayed here):";
        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::SlotCapacity; ++slot)
        {
            if (RecordAt(slot).SpawnedFrames == 0)
            {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(slot);
            std::string entry = " [";
            entry += ::MphRead::NativeRuntime::ToString(slot);
            entry += "] ";
            const std::int32_t resolved = ManagedAt(NetDamageMetrics::Read().Resolved, index);
            entry += ::MphRead::NativeRuntime::ToString(resolved);
            entry += '/';
            const std::int32_t replayed = ManagedAt(NetDamageMetrics::Read().Replayed, index);
            entry += ::MphRead::NativeRuntime::ToString(replayed);
            pipeline += entry;
        }
        ConsoleWriteLine(pipeline);

        std::string fired = "    shots spawned here (per slot):";
        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::SlotCapacity; ++slot)
        {
            if (RecordAt(slot).SpawnedFrames == 0)
            {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(slot);
            const std::int32_t conditionFired = ManagedAt(NetDamageMetrics::Read().Fired, index);
            double avg = 0.0;
            if (conditionFired > 0)
            {
                const double aimDrift = ManagedAt(NetDamageMetrics::Read().AimDrift, index);
                const std::int32_t divisorFired = ManagedAt(NetDamageMetrics::Read().Fired, index);
                avg = aimDrift / divisorFired;
            }
            std::string entry = " [";
            entry += ::MphRead::NativeRuntime::ToString(slot);
            entry += "] ";
            const std::int32_t displayedFired = ManagedAt(NetDamageMetrics::Read().Fired, index);
            entry += ::MphRead::NativeRuntime::ToString(displayedFired);
            entry += "(drift ";
            entry += ::MphRead::NativeRuntime::ToString(avg, "0.0");
            entry += '/';
            const double worstDrift = ManagedAt(NetDamageMetrics::Read().WorstDrift, index);
            entry += ::MphRead::NativeRuntime::ToString(worstDrift, "0.0");
            entry += " deg)";
            fired += entry;
        }
        ConsoleWriteLine(fired);

        std::string collision = "    player collision checks:";
        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::SlotCapacity; ++slot)
        {
            if (RecordAt(slot).SpawnedFrames == 0)
            {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(slot);
            std::string entry = " [";
            entry += ::MphRead::NativeRuntime::ToString(slot);
            entry += "] ";
            const std::int32_t checks = ManagedAt(NetDamageMetrics::Read().PlayerChecks, index);
            entry += ::MphRead::NativeRuntime::ToString(checks);
            entry += '/';
            const std::int32_t overlaps = ManagedAt(NetDamageMetrics::Read().PlayerOverlaps, index);
            entry += ::MphRead::NativeRuntime::ToString(overlaps);
            entry += '/';
            const std::int32_t accepted = ManagedAt(NetDamageMetrics::Read().PlayerAccepted, index);
            entry += ::MphRead::NativeRuntime::ToString(accepted);
            collision += entry;
        }
        ConsoleWriteLine(collision);

        ConsoleWriteLine("    " + NetPlayerBridge::DescribeConfirms());
        ConsoleWriteLine("    hits taken that were seen landing: " + ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().HitsTakenSeen)
            + " of " + ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().HitsTaken) + " (remote aims turned onto this player on "
            + ::MphRead::NativeRuntime::ToString(NetPlayerBridge::AimsRetargeted()) + " frames, "
            + ::MphRead::NativeRuntime::ToString(NetPlayerBridge::ShotsSteered()) + " shots in flight steered onto it)");
        {
            static constexpr const char* names[] = {"PowerBeam", "VoltDriver", "Missile", "Battlehammer",
                "Imperialist", "Judicator", "Magmaul", "ShockCoil", "OmegaCannon", "Platform", "other"};
            std::string byBeam = "    seen landing by weapon:";
            for (std::size_t i = 0; i < NetDamageMetrics::Read().HitsTakenByBeam.size(); i++)
            {
                if (NetDamageMetrics::Read().HitsTakenByBeam[i] > 0)
                {
                    byBeam += std::string(" ") + names[i] + " " + ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().HitsTakenSeenByBeam[i])
                        + "/" + ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().HitsTakenByBeam[i]);
                }
            }
            ConsoleWriteLine(byBeam);
        }
        std::string pairs = "    player overlaps by shooter:";
        for (std::int32_t shooter = 0; shooter < Entities::PlayerEntity::SlotCapacity; ++shooter)
        {
            for (std::int32_t target = 0; target < Entities::PlayerEntity::SlotCapacity; ++target)
            {
                const std::int32_t count = ManagedAt(
                    ManagedAt(NetDamageMetrics::Read().PlayerOverlapsByShooter, shooter), target);
                if (count > 0)
                {
                    std::string entry = " [";
                    entry += ::MphRead::NativeRuntime::ToString(shooter);
                    entry += "->";
                    entry += ::MphRead::NativeRuntime::ToString(target);
                    entry += "] ";
                    entry += ::MphRead::NativeRuntime::ToString(count);
                    pairs += entry;
                }
            }
        }
        ConsoleWriteLine(pairs);

        std::string authority = "    authority for ";
        const std::int64_t authorityFrames = NetSession::AuthorityFrames();
        authority += ::MphRead::NativeRuntime::ToString(authorityFrames);
        authority += " frame(s) of ";
        const std::uint32_t netFrame = NetSession::NetFrame();
        authority += ::MphRead::NativeRuntime::ToString(netFrame);
        ConsoleWriteLine(authority);

        std::string snaps = "    remote position snaps: ";
        const std::int64_t snapCount = NetPlayerBridge::Snaps();
        snaps += ::MphRead::NativeRuntime::ToString(snapCount);
        snaps += " (worst ";
        const float worstSnap = NetPlayerBridge::WorstSnap();
        snaps += ::MphRead::NativeRuntime::ToString(worstSnap, "0.0");
        snaps += " units) -- these are the visible teleports";
        ConsoleWriteLine(snaps);

        std::string unresolved = "    node lookups unresolved: ";
        const std::int64_t unresolvedCount = NetPlayerBridge::UnresolvedNodeLookupCount();
        unresolved += ::MphRead::NativeRuntime::ToString(unresolvedCount);
        unresolved += " -- these players are drawn uncalled";
        ConsoleWriteLine(unresolved);
        // The respawn this client took for itself, which is where two
        // separate bugs lived and which the tour could not reach until it
        // learned to blow itself up.
        ConsoleWriteLine("    own respawn: "
            + ::MphRead::NativeRuntime::ToString(NetHitPrediction::SelfDeathsPredicted())
            + " self-kill(s) predicted, "
            + ::MphRead::NativeRuntime::ToString(NetPlayerBridge::SpawnFacingsTurnedCount())
            + " spawn facing(s) turned "
            + "(worst " + ::MphRead::NativeRuntime::ToString(NetPlayerBridge::WorstSpawnFacingDegrees(), "0.0") + " deg), "
            + ::MphRead::NativeRuntime::ToString(NetPlayerBridge::StaleDeathsIgnoredCount())
            + " stale death(s) ignored");
        if (NetHitPrediction::SelfDeathsPredicted() == 0)
        {
            // Said out loud rather than left as a zero: "never exercised" is
            // not a pass.
            ConsoleWriteLine("    note: nobody killed themselves, so the early respawn "
                "-- and everything that only goes wrong there -- went untested");
        }

        if (NetPlayerBridge::RejectedUpdates() > 0)
        {
            std::string rejected = "    FAIL: ";
            const std::int64_t rejectedCount = NetPlayerBridge::RejectedUpdates();
            rejected += ::MphRead::NativeRuntime::ToString(rejectedCount);
            rejected += " update(s) rejected for holding impossible values";
            ConsoleWriteLine(rejected);
            IncrementInPlace(fails);
        }

        failures = fails;
        return fails == 0;
    }

    void NetFeatureCheck::SampleScoreboard(std::int32_t serverSecond)
    {
        std::string prefix = "    scoreboard at t=";
        prefix += ::MphRead::NativeRuntime::ToString(serverSecond);
        prefix += "s:";
        const std::string board = Scoreboard(prefix);
        Boards[serverSecond] = board;
    }

    std::string NetFeatureCheck::Scoreboard(const std::string& prefix) const
    {
        std::string board = prefix;
        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            if (RecordAt(slot).SpawnedFrames == 0)
            {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(slot);
            std::string entry = " [";
            entry += ::MphRead::NativeRuntime::ToString(slot);
            entry += "] ";
            const std::string nickname = ManagedAt(GameState::Nicknames(), index);
            entry += nickname;
            entry += ' ';
            const std::int32_t kills = ManagedAt(GameState::Kills(), index);
            entry += ::MphRead::NativeRuntime::ToString(kills);
            entry += "k/";
            const std::int32_t deaths = ManagedAt(GameState::Deaths(), index);
            entry += ::MphRead::NativeRuntime::ToString(deaths);
            entry += "d/";
            const std::int32_t points = ManagedAt(GameState::Points(), index);
            entry += ::MphRead::NativeRuntime::ToString(points);
            entry += 'p';
            board += entry;
        }
        return board;
    }

    std::int32_t NetFeatureCheck::ReportOne(
        const Record& mine, const Record& other, const std::string& them) const
    {
        std::string report;
        std::int32_t fails = 0;

        // My tour against what I saw of theirs: two players, one script. A
        // feature I did that I never saw them do is unconfirmed, not failed --
        // they may never have done it (stuck in the ball, dead, elsewhere).
        // Whether what a player did reached everyone is compare-reports.py's
        // call, which pairs each player with their own record.
        for (const Feature& feature : _features)
        {
            if (feature.Needed <= 0)
            {
                continue;
            }
            const double self = feature.Get(mine);
            const double seen = feature.Get(other);
            const bool applicable = feature.Applies == nullptr || feature.Applies(other.Hunter);
            const bool tested = applicable
                && (feature.Pairwise ? self + seen >= feature.Needed : self >= feature.Needed);
            const char* verdict = !applicable ? "n/a"
                : !tested ? "untested"
                : feature.Pairwise || seen >= feature.Needed ? "ok"
                : "unconfirmed";

            std::string text = "    ";
            text += PadRightManaged(feature.Name, 16);
            text += " mine ";
            text += PadLeftManaged(::MphRead::NativeRuntime::ToString(self, "0"), 7);
            text += ' ';
            text += PadRightManaged(feature.Unit, 11);
            text += " theirs ";
            text += PadLeftManaged(::MphRead::NativeRuntime::ToString(seen, "0"), 7);
            text += ' ';
            text += PadRightManaged(feature.Unit, 11);
            text += ' ';
            text += verdict;
            report += text;
            report += EnvironmentNewLine();
        }

        ConsoleWrite(report);

        std::string jump = "    ";
        jump += them;
        jump += ": ";
        jump += ::MphRead::NativeRuntime::ToString(other.Teleports);
        jump += " teleport(s), worst jump ";
        jump += ::MphRead::NativeRuntime::ToString(other.WorstStep, "0.0");
        jump += " units";
        ConsoleWriteLine(jump);

        if (!other.EverCompared)
        {
            const std::string agreement = "    " + them
                + ": form and position agreement not measured here -- "
                + "this client is the authority and receives no snapshot to compare with";
            ConsoleWriteLine(agreement);
        }
        else
        {
            std::string agreement = "    ";
            agreement += them;
            agreement += ": form disagreed on ";
            agreement += ::MphRead::NativeRuntime::ToString(other.FormDisagreeFrames);
            agreement += " frame(s) (longest run ";
            agreement += ::MphRead::NativeRuntime::ToString(other.WorstFormDisagreeRun);
            agreement += "), worst position gap ";
            agreement += ::MphRead::NativeRuntime::ToString(other.WorstPositionGap, "0.00");
            agreement += " units";
            ConsoleWriteLine(agreement);
        }

        if (static_cast<std::uint32_t>(other.WorstFormDisagreeRun)
            > FormReconciliation::MaximumDisagreementFrames)
        {
            std::string failure = "    FAIL: their form stayed wrong for ";
            failure += ::MphRead::NativeRuntime::ToString(other.WorstFormDisagreeRun);
            failure += " frames in a row -- ";
            failure += other.WorstFormContext;
            ConsoleWriteLine(failure);
            IncrementInPlace(fails);
        }
        if (other.WorstPositionGap > 8.0
            || !std::isfinite(other.WorstPositionGap))
        {
            ConsoleWriteLine(
                "    FAIL: their position drifted far from the authority's");
            IncrementInPlace(fails);
        }
        return fails;
    }

    bool NetFeatureCheck::IsWeavel(Hunter hunter) noexcept
    {
        return hunter == Hunter::Weavel;
    }

    bool NetFeatureCheck::LaysBombs(Hunter hunter) noexcept
    {
        return hunter == Hunter::Samus
            || hunter == Hunter::Kanden
            || hunter == Hunter::Sylux;
    }

    double NetFeatureCheck::Height(const Record& record) noexcept
    {
        return record.MaxY > record.MinY
            ? static_cast<double>(record.MaxY - record.MinY)
            : 0.0;
    }
}
