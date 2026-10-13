#pragma once

#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Diagnostics/NetDamageMetrics.hpp"
#include "Mods/Network/Diagnostics/NetShotDiagnostics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace MphRead::Mods::Network
{
    class NetDamage final
    {
    public:
        NetDamage() = delete;

        static constexpr std::uint8_t NoSlot = 0xFF;
        static constexpr std::uint8_t NoBeam = 0xFF;

        [[nodiscard]] static bool Replaying() noexcept { return _replaying; }
        [[nodiscard]] static MphRead::BeamType ReplayBeam() noexcept { return _replayBeam; }

        static void NoteFired(Entities::PlayerEntity& shooter,
            OpenTK::Mathematics::Vector3 shotVec, OpenTK::Mathematics::Vector3 aimVec);
        static void NotePlayerOverlap(Entities::EntityBase* owner, Entities::PlayerEntity& target);

        static void ResetForRoomChange();
        static void ForgetSlot(std::int32_t slot);
        static void NoteRespawn(std::int32_t slot, std::uint16_t sequence);
        static void Reset();

        [[nodiscard]] static bool Suppress(Entities::PlayerEntity& victim,
            Entities::EntityBase* source, Entities::DamageFlags flags);
        // Whether a hit by `source` on `victim` is applied on this machine,
        // rather than left to its shooter's claim or refused: what Suppress
        // lets through, and what a life-draining hit's heal follows -- a
        // drain the hit is not resolved here would be this machine's guess.
        [[nodiscard]] static bool ResolvedHere(Entities::PlayerEntity& victim,
            Entities::EntityBase* source, Entities::DamageFlags flags);

        static void SetClaimedBeam(MphRead::BeamType beam) noexcept { _claimedBeam = beam; }
        [[nodiscard]] static bool ApplyingClaim() noexcept { return _applyingClaim; }

        // using (new ClaimScope(beam)) { ... }
        class ClaimScope final
        {
        public:
            explicit ClaimScope(MphRead::BeamType beam) noexcept
            {
                _claimedBeam = beam;
                _applyingClaim = true;
            }
            ~ClaimScope()
            {
                _claimedBeam = MphRead::BeamType::None;
                _applyingClaim = false;
            }
            ClaimScope(const ClaimScope&) = delete;
            ClaimScope& operator=(const ClaimScope&) = delete;
        };

        static void Note(Entities::PlayerEntity& victim, Entities::PlayerEntity* attacker,
            MphRead::BeamType beam, Entities::DamageFlags flags,
            std::optional<OpenTK::Mathematics::Vector3> direction,
            std::uint32_t amount = 0, bool fromBomb = false,
            std::uint32_t launchFrame = 0, std::optional<ShotKey> launchKey = std::nullopt);

        // using (new PredictionScoreScope(active)) { ... }
        class PredictionScoreScope final
        {
        public:
            explicit PredictionScoreScope(bool active) : _active(active)
            {
                if (active)
                {
                    const bool outermost = _predictionScoreDepth == 0;
                    ::MphRead::NativeRuntime::IncrementInPlace(_predictionScoreDepth);
                    if (outermost)
                    {
                        SaveScores();
                    }
                }
            }
            ~PredictionScoreScope()
            {
                if (_active)
                {
                    ::MphRead::NativeRuntime::DecrementInPlace(_predictionScoreDepth);
                    if (_predictionScoreDepth == 0)
                    {
                        RestoreScores();
                    }
                }
            }
            PredictionScoreScope(const PredictionScoreScope&) = delete;
            PredictionScoreScope& operator=(const PredictionScoreScope&) = delete;

        private:
            bool _active;
        };

        static void ReplayDeath(Entities::PlayerEntity& player);

        static void Write(std::int32_t slot, PlayerState& state);
        static void BeginLife(std::int32_t slot, const PlayerState& state);
        static void Replay(Entities::PlayerEntity& player, const PlayerState& state);

    private:
        [[nodiscard]] static bool Refuses(Entities::PlayerEntity& victim,
            Entities::EntityBase* source, Entities::DamageFlags flags, bool noting);
        static constexpr std::int32_t Slots = Entities::PlayerEntity::SlotCapacity;
        static constexpr std::int32_t RelayedFlags
            = static_cast<std::int32_t>(Entities::DamageFlags::Headshot)
            | static_cast<std::int32_t>(Entities::DamageFlags::Deathalt)
            | static_cast<std::int32_t>(Entities::DamageFlags::Burn);
        static constexpr float MaxImpulse = 1.5F;

        inline static std::array<std::uint16_t, Slots> _sequence{};
        inline static std::array<std::array<DamageEvent, PlayerState::DamageHistory>, Slots> _history{};
        inline static std::array<std::uint8_t, Slots> _attacker = [] {
            std::array<std::uint8_t, Slots> value{};
            value.fill(NoSlot);
            return value;
        }();
        inline static std::array<std::uint8_t, Slots> _beam = [] {
            std::array<std::uint8_t, Slots> value{};
            value.fill(NoBeam);
            return value;
        }();
        inline static std::array<std::uint8_t, Slots> _flags{};
        inline static std::array<OpenTK::Mathematics::Vector3, Slots> _direction{};
        inline static std::array<std::uint16_t, Slots> _lastLife{};
        inline static std::array<std::uint16_t, Slots> _lastGeneration{};
        inline static std::array<std::uint16_t, Slots> _lastSeen{};
        inline static std::array<bool, Slots> _everSeen{};
        // The newest damage event's shot and confirmed impact, per victim.
        inline static std::array<std::uint8_t, Slots> _impactLaunch{};
        inline static std::array<ImpactOffset, Slots> _impact{};

        inline static bool _replaying = false;
        inline static MphRead::BeamType _replayBeam = MphRead::BeamType::None;
        inline static MphRead::BeamType _claimedBeam = MphRead::BeamType::None;
        inline static bool _applyingClaim = false;

        inline static std::array<std::int32_t, Slots> _savedPoints{};
        inline static std::array<std::int32_t, Slots> _savedKills{};
        inline static std::array<std::int32_t, Slots> _savedDeaths{};
        inline static std::int32_t _predictionScoreDepth = 0;

        [[nodiscard]] static OpenTK::Mathematics::Vector3 ClampImpulse(
            OpenTK::Mathematics::Vector3 impulse);
        static void SaveScores();
        static void RestoreScores();
        // launchLow: the low byte of the shot's launch frame, when the snapshot
        // names it -- only for the newest hit it carries.
        static void ReplayEvent(Entities::PlayerEntity& player, const PlayerState& state,
            std::optional<std::uint8_t> launchLow);
    };
}
