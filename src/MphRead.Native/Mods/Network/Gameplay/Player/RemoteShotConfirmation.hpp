#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Entities
{
    class BeamProjectileEntity;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Matches an authority-confirmed impact to the remote projectile that
    // represents it on this client, including shots that arrive late or are
    // already gone when the damage event does.
    class RemoteShotConfirmation final
    {
    public:
        RemoteShotConfirmation() = delete;

        static void SetScene(MphRead::Scene* scene) noexcept { _scene = scene; }
        static void ConfirmedImpacts(bool value) noexcept { _confirmedImpacts = value; }
        [[nodiscard]] static bool ConfirmedImpacts() noexcept { return _confirmedImpacts; }
        static void ObservedImpacts(bool value) noexcept { _observedImpacts = value; }
        static void ConfirmIncoming(std::int32_t attackerSlot, std::int32_t victimSlot, std::uint8_t beam,
            bool keyed, std::uint8_t launchLow, ImpactOffset impact, bool headshot = false);
        [[nodiscard]] static bool CoilAimFor(const Entities::PlayerEntity& shooter,
            OpenTK::Mathematics::Vector3 muzzle, OpenTK::Mathematics::Vector3& aim);
        static void OnRemoteShotSpawned(Entities::BeamProjectileEntity& beam);
        static void NoteRemoteShotGone(const Entities::BeamProjectileEntity& beam);
        [[nodiscard]] static bool PassesThrough(Entities::BeamProjectileEntity& beam,
            const Entities::PlayerEntity& player);
        static void TickConfirms();
        [[nodiscard]] static std::uint32_t HoldFrames() noexcept;
        static void NoteConfirmDelay(std::uint32_t frames) noexcept;
        static void NotePassedLocal(const Entities::BeamProjectileEntity& beam);
        static void SynthesizeImpact(std::int32_t attackerSlot, std::int32_t victimSlot, std::uint8_t beam,
            std::uint8_t launchLow, OpenTK::Mathematics::Vector3 offset, bool headshot = false);
        [[nodiscard]] static std::string DescribeConfirms();

    private:
        struct Pending
        {
            std::int32_t Attacker = -1;
            std::uint8_t LaunchLow = 0;
            std::uint8_t Beam = 0;
            OpenTK::Mathematics::Vector3 Offset{};
            std::uint32_t Until = 0;
            bool Live = false;
            bool Headshot = false;
            std::int32_t Victim = -1;
        };

        struct GoneShot
        {
            std::int32_t Attacker = -1;
            std::uint8_t LaunchLow = 0;
            std::uint32_t Frame = 0;
            OpenTK::Mathematics::Vector3 Where{};
            float Blast = 0;
        };

        static constexpr std::uint32_t ConfirmWaitFrames = 10;
        static MphRead::Scene* _scene;
        static bool _confirmedImpacts;
        static std::array<Pending, 16> _pendingConfirms;
        static std::array<GoneShot, 32> _goneShots;
        static std::size_t _goneNext;
        static std::int64_t _confirmsInFlight;
        static std::int64_t _confirmsAtSpawn;
        static std::int64_t _impactsSynthesized;
        static std::int64_t _passedThrough;
        static std::int64_t _confirmsIgnored;
        static double _homeAngleSum;
        static double _homeAngleMax;
        static bool _observedImpacts;
        static std::array<std::int32_t, 8> _coilVictim;
        static std::array<OpenTK::Mathematics::Vector3, 8> _coilOffset;
        static std::array<std::uint32_t, 8> _coilUntil;
        static std::int64_t _coilTicks;
        static std::int64_t _unkeyedImpacts;
        static std::int64_t _observedConfirms;
        static std::int64_t _seenAsBlast;
        static std::array<std::uint8_t, 64> _confirmDelays;
        static std::size_t _confirmDelayCount;
        static std::size_t _confirmDelayNext;

        [[nodiscard]] static const GoneShot* RecentlyGone(std::int32_t attackerSlot, std::uint8_t launchLow);
    };
}
