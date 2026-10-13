#include "Mods/Network/Server/Match/ServerIntentValidator.hpp"

#include <cstddef>

namespace MphRead::Mods::Network
{
    std::optional<IntentPacket> ServerIntentValidator::Validate(
        std::span<const std::uint8_t> payload, std::uint16_t matchId,
        std::uint64_t authorityEpoch, std::uint16_t slotGeneration,
        std::uint16_t lifeId)
    {
        if (payload.size() < static_cast<std::size_t>(IntentPacket::Size)
            || payload.size() > static_cast<std::size_t>(IntentPacket::MaxWireSize))
        {
            return std::nullopt;
        }

        IntentPacket intent = IntentPacket::Read(payload);
        if (intent.MatchId != matchId || intent.AuthorityEpoch != authorityEpoch
            || intent.SlotGeneration != slotGeneration || intent.LifeId != lifeId)
        {
            return std::nullopt;
        }
        return intent;
    }
}
