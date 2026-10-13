#pragma once

#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Gameplay/Replication/FormReconciliation.hpp"
#include "Mods/Network/Gameplay/Player/RemoteShotConfirmation.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Entities
{
    class Keybind;
    class PlayerControls;
    class BeamProjectileEntity;
}

namespace MphRead::Mods::Network
{
    class NetPlayerBridge final
    {
    public:
        NetPlayerBridge() = delete;

        [[nodiscard]] static std::int32_t PlacementsRefusedCount() noexcept { return PlacementsRefused; }
        [[nodiscard]] static std::int32_t SpawnFacingsTurnedCount() noexcept { return SpawnFacingsTurned; }
        [[nodiscard]] static float WorstSpawnFacingDegrees() noexcept { return WorstSpawnFacing; }
        [[nodiscard]] static std::int32_t StaleDeathsIgnoredCount() noexcept { return StaleDeathsIgnored; }
        [[nodiscard]] static std::int64_t UnresolvedNodeLookupCount() noexcept { return NodeLookupsUnresolved; }
        static void RecordUnresolvedNodeLookup() noexcept;

        [[nodiscard]] static std::string FormSaidByAuthority();

        [[nodiscard]] static std::int64_t RejectedUpdates() noexcept { return _rejectedUpdates; }
        [[nodiscard]] static std::int64_t Snaps() noexcept { return _snaps; }
        [[nodiscard]] static float WorstSnap() noexcept { return _worstSnap; }

        [[nodiscard]] static OpenTK::Mathematics::Vector3 InFormFor(
            Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 position, bool measuredInAlt);

        static void RecordPresses(Entities::PlayerEntity& player);
        // What any player's copy is doing this frame, as an intent: the
        // buttons its controls hold, its aim, position, weapon, ammo and
        // charge. Reads the player and nothing else.
        [[nodiscard]] static IntentPacket StateIntent(Entities::PlayerEntity& player);
        // This machine's own player's intent: StateIntent and what only it
        // has -- its press history, a latched charge, the world it has seen.
        [[nodiscard]] static IntentPacket CaptureIntent(Entities::PlayerEntity& player);

        [[nodiscard]] static bool RespawnRequested(std::int32_t slot);
        [[nodiscard]] static std::int32_t ShootPressAgeFor(std::int32_t slot) noexcept;
        static void ApplyIntent(Entities::PlayerEntity& player, const IntentPacket& intent);
        static void NoteSpawn(std::int32_t slot);
        [[nodiscard]] static std::uint32_t SpawnFrameFor(std::int32_t slot) noexcept;
        [[nodiscard]] static bool AimTrusted(std::int32_t slot);
        [[nodiscard]] static bool AimAvailable(std::int32_t slot);

        static void ApplyState(Entities::PlayerEntity& player, const PlayerState& state, bool isLocal);
        [[nodiscard]] static FormCorrection ReconcileForm(std::int32_t slot, std::uint32_t frame, bool desiredAlt,
            bool actualAlt, bool morphing, bool unmorphing, std::int32_t ping);

        static void NoteRoomChanged();
        static void Reset();
        static void ForgetSlot(std::int32_t slot);
        // The ray this machine's own player fired on this frame, after spread.
        static void NoteLocalShot(OpenTK::Mathematics::Vector3 origin, OpenTK::Mathematics::Vector3 direction) noexcept;
        static void AttachLocalShot(IntentPacket& intent) noexcept;
        // The victim's view of a remote shot. A remote player's aim is the
        // one they had on their own screen, where this player stood a round
        // trip ago; drawn as is, the shot sails past and the damage lands
        // anyway. When that aim was on this player in the world the shooter
        // was looking at, it is turned onto where this player is now, so the
        // shot that hits is also the shot that is seen hitting.
        [[nodiscard]] static OpenTK::Mathematics::Vector3 RetargetAtLocal(const Entities::PlayerEntity& shooter,
            OpenTK::Mathematics::Vector3 origin, OpenTK::Mathematics::Vector3 aim, std::uint32_t ackFrame,
            OpenTK::Mathematics::Vector3 aimedFrom);
        // The shooter's own ray, when the intent carries it, else its aim
        // from where it reported standing (plus the drawn gun's offset).
        static void ShooterRay(const Entities::PlayerEntity& shooter, OpenTK::Mathematics::Vector3 drawnMuzzle,
            OpenTK::Mathematics::Vector3& from, OpenTK::Mathematics::Vector3& direction, std::uint32_t& ackFrame);
        [[nodiscard]] static std::int64_t AimsRetargeted() noexcept { return _aimsRetargeted; }
        // The authority says that player hit this one: if one of their shots
        // drawn here is still in the air, it is the one, and it is turned onto
        // this player so the hit is seen arriving with the damage.
        static void SteerIncoming(std::int32_t attackerSlot);

        // Confirmed impacts (protocol 18). The authority's damage names the
        // shot (the low byte of its launch frame) and where on this player the
        // shooter saw it land. That one shot is the one drawn arriving there:
        // homed onto the spot if it is in the air here, homed from the moment
        // it appears if it has not appeared yet, and drawn as an impact on the
        // spot if it is already gone. A remote shot nobody confirmed passes
        // through this player instead of being drawn hitting them -- the
        // impacts the shooter never made.
        static void SetScene(MphRead::Scene* scene) noexcept { RemoteShotConfirmation::SetScene(scene); }
        static void ConfirmedImpacts(bool value) noexcept { RemoteShotConfirmation::ConfirmedImpacts(value); }
        [[nodiscard]] static bool ConfirmedImpacts() noexcept { return RemoteShotConfirmation::ConfirmedImpacts(); }
        // keyed: the damage event is the player state's newest, the one the
        // launch byte and impact belong to; an older one is drawn at the chest.
        static void ConfirmIncoming(std::int32_t attackerSlot, std::int32_t victimSlot, std::uint8_t beam,
            bool keyed, std::uint8_t launchLow, ImpactOffset impact, bool headshot = false)
        { RemoteShotConfirmation::ConfirmIncoming(attackerSlot, victimSlot, beam, keyed, launchLow, impact, headshot); }
        // Confirmed impacts drawn between two other players too (this
        // machine watching A hit B); -noobservedimpacts keeps them to this player.
        static void ObservedImpacts(bool value) noexcept { RemoteShotConfirmation::ObservedImpacts(value); }
        // Compatibility facade for the confirmed-shot subsystem.
        static bool CoilAimFor(const Entities::PlayerEntity& shooter, OpenTK::Mathematics::Vector3 muzzle,
            OpenTK::Mathematics::Vector3& aim)
        { return RemoteShotConfirmation::CoilAimFor(shooter, muzzle, aim); }
        static void OnRemoteShotSpawned(Entities::BeamProjectileEntity& beam)
        { RemoteShotConfirmation::OnRemoteShotSpawned(beam); }
        static void NoteRemoteShotGone(const Entities::BeamProjectileEntity& beam)
        { RemoteShotConfirmation::NoteRemoteShotGone(beam); }
        // Called for every player a projectile meets: true when this is a
        // remote shot nobody has confirmed against that player.
        [[nodiscard]] static bool PassesThrough(Entities::BeamProjectileEntity& beam,
            const Entities::PlayerEntity& player)
        { return RemoteShotConfirmation::PassesThrough(beam, player); }
        static void TickConfirms() { RemoteShotConfirmation::TickConfirms(); }
        // How long a remote shot that met a player waits for the word: the
        // 90th percentile of how long the word actually took on this line
        // (the last 64 held shots that were confirmed), plus one, within 2-8;
        // 6 until 16 have been seen. A miss is held no longer than it must be.
        [[nodiscard]] static std::uint32_t HoldFrames() noexcept { return RemoteShotConfirmation::HoldFrames(); }
        static void NoteConfirmDelay(std::uint32_t frames) noexcept { RemoteShotConfirmation::NoteConfirmDelay(frames); }
        static void NotePassedLocal(const Entities::BeamProjectileEntity& beam)
        { RemoteShotConfirmation::NotePassedLocal(beam); }
        static void SynthesizeImpact(std::int32_t attackerSlot, std::int32_t victimSlot, std::uint8_t beam,
            std::uint8_t launchLow, OpenTK::Mathematics::Vector3 offset, bool headshot = false)
        { RemoteShotConfirmation::SynthesizeImpact(attackerSlot, victimSlot, beam, launchLow, offset, headshot); }
        [[nodiscard]] static std::string DescribeConfirms() { return RemoteShotConfirmation::DescribeConfirms(); }
        [[nodiscard]] static std::int64_t ShotsSteered() noexcept { return _shotsSteered; }
        static void RetargetEnabled(bool value) noexcept { _retargetEnabled = value; }
        [[nodiscard]] static std::int64_t KillsResynced() noexcept { return _killsResynced; }
        // Apply the next snapshot of this slot as a fresh life: used when a
        // kill shown here is refused, so the player is put back where and as
        // the authority has them.
        static void Resync(std::int32_t slot) noexcept
        {
            if (slot >= 0 && slot < Slots) _lifeApplied[static_cast<std::size_t>(slot)] = false;
        }

        static void ApplyReportedPosition(Entities::PlayerEntity& player, const IntentPacket& intent);
        static void RestoreSnapshotPosition(Entities::PlayerEntity& player, const PlayerState& state);
        static void RestoreReportedPosition(Entities::PlayerEntity& player, const IntentPacket& intent);

    private:
        inline static std::int32_t PlacementsRefused = 0;
        inline static std::int32_t SpawnFacingsTurned = 0;
        inline static float WorstSpawnFacing = 0.0F;
        inline static std::int32_t StaleDeathsIgnored = 0;
        inline static std::int64_t NodeLookupsUnresolved = 0;
        inline static std::array<std::int32_t, Entities::PlayerEntity::SlotCapacity> ShootPressAge{};
        inline static std::array<std::uint32_t, Entities::PlayerEntity::SlotCapacity> SpawnFrame{};

        static constexpr std::int32_t Slots = Entities::PlayerEntity::SlotCapacity;
        static constexpr float SnapDistance = 15.0F;
        [[maybe_unused]] static constexpr float CatchUpRate = 0.35F;
        [[maybe_unused]] static constexpr float FastCatchUpRate = 0.6F;
        [[maybe_unused]] static constexpr float FastCatchUpAbove = 3.0F;
        static constexpr float DesyncDistance = 30.0F;
        static constexpr float MaxReportedSpeed = 5.0F;
        static constexpr std::uint32_t AimHoldCeiling = 90;
        static constexpr std::int32_t DivergedFramesBeforeCorrecting = 60;
        static constexpr IntentButtons PressedButtons = ~(
            IntentButtons::ZoomedState | IntentButtons::AltFormState
            | IntentButtons::InPlayState | IntentButtons::SpectatingState
            | IntentButtons::ReadyState);

        [[nodiscard]] static OpenTK::Mathematics::Vector3 InForm(
            Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 position, bool measuredInAlt);
        static void Set(Entities::Keybind& bind, bool down, bool pressed = false);
        static void BeginRemoteLife(Entities::PlayerEntity& player, const PlayerState& state);
        [[nodiscard]] static bool Diverged(Entities::PlayerEntity& player, const PlayerState& state, std::int32_t slot);
        [[nodiscard]] static bool StaleSinceSpawn(Entities::PlayerEntity& player, const IntentPacket& intent);
        static void NoteReportedVelocity(Entities::PlayerEntity& player,
            OpenTK::Mathematics::Vector3 reported, std::uint32_t frame);
        [[nodiscard]] static bool FrozenInPlace(Entities::PlayerEntity& player);
        static void Move(Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 position,
            bool resolveNodeFromPosition = false);

        inline static std::int64_t _rejectedUpdates = 0;
        inline static std::int64_t _snaps = 0;
        inline static float _worstSnap = 0.0F;

        inline static std::int32_t _latchedCharge = 0;
        inline static std::int32_t _latchedBoostDamage = 0;
        inline static bool _hasLatch = false;

        inline static std::array<bool, Slots> _respawnRequested{};
        inline static std::array<bool, Slots> _aimHeld{};

        inline static std::array<std::uint16_t, Slots> _appliedLifeId{};
        inline static std::array<bool, Slots> _lifeApplied{};
        inline static std::int64_t _killsResynced = 0;
        static constexpr std::int32_t LocalHistory = 128;
        inline static std::array<std::uint32_t, LocalHistory> _localFrames{};
        inline static std::array<OpenTK::Mathematics::Vector3, LocalHistory> _localPositions{};
        inline static std::int64_t _aimsRetargeted = 0;
        inline static std::int64_t _shotsSteered = 0;
        inline static bool _retargetEnabled = true;
        inline static std::uint32_t _localShotFrame = 0;
        inline static OpenTK::Mathematics::Vector3 _localShotOrigin{};
        inline static OpenTK::Mathematics::Vector3 _localShotDirection{};
        inline static std::uint32_t _lastRetargetFrame = 0;
        inline static std::array<std::int32_t, Slots> _divergedFrames{};

        inline static std::array<OpenTK::Mathematics::Vector3, Slots> _lastReportPosition{};
        inline static std::array<std::uint32_t, Slots> _lastReportFrame{};
        inline static std::array<bool, Slots> _reportSeen{};
    };
}
