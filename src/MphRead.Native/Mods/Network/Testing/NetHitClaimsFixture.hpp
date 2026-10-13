#pragma once

#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"

namespace MphRead::Mods::Network
{
    // Keeps combat diagnostics' authority-only probes out of the production
    // claim API while exposing only the small set of private rules they need.
    class NetHitClaimsFixture final
    {
    public:
        [[nodiscard]] static std::uint8_t Judge(
            std::int32_t shooterSlot, const HitClaimPacket& claim)
        {
            return NetHitClaims::Judge(shooterSlot, claim);
        }

        static void NoteRescued(std::int32_t attacker, std::int32_t victim, std::uint32_t launch)
        {
            NetHitClaims::NoteRescued(attacker, victim, launch);
        }

        static void NoteLedger(std::int32_t attacker, std::int32_t victim,
            std::uint32_t ack, std::uint32_t launch, std::int32_t damage, bool used = false,
            BeamType beam = BeamType::None, std::optional<ShotKey> launchKey = std::nullopt)
        {
            NetHitClaims::NoteLedger(attacker, victim, ack, launch, damage, used, beam, launchKey);
        }

        [[nodiscard]] static std::int32_t MaxDamageFor(std::int32_t shooterSlot, std::uint8_t beam)
        {
            return NetHitClaims::MaxDamageFor(shooterSlot, beam);
        }
    };
}
