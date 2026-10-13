#include "Mods/Network/Server/Match/ServerSnapshotValidator.hpp"

#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Session/NetMatchTimeSync.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"

#include <cstddef>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::optional<ValidatedServerSnapshot> ServerSnapshotValidator::Validate(
        std::span<const std::uint8_t> payload,
        std::uint16_t matchId,
        std::uint64_t authorityEpoch,
        const ServerSlotIdentity& slotIdentity)
    {
        if (payload.size() < static_cast<std::size_t>(SnapshotHeader::Size))
        {
            return std::nullopt;
        }

        ValidatedServerSnapshot validated{};
        validated.Header = SnapshotHeader::Read(payload);
        const SnapshotHeader& header = validated.Header;
        if (header.MatchId != matchId || header.AuthorityEpoch != authorityEpoch
            || header.PlayerCount > ServerSlotIdentity::SlotCapacity)
        {
            return std::nullopt;
        }

        const std::size_t timeOffset = SnapshotHeader::Size
            + static_cast<std::size_t>(header.PlayerCount) * PlayerState::Size;
        const std::size_t itemOffset = timeOffset + NetMatchTimeSync::Size;
        if (itemOffset > payload.size())
        {
            return std::nullopt;
        }

        const std::span<const std::uint8_t> timePayload =
            payload.subspan(timeOffset, NetMatchTimeSync::Size);
        const std::span<const std::uint8_t> itemPayload = payload.subspan(itemOffset);
        if (!NetMatchTimeSync::Validate(timePayload) || !NetItemState::Validate(itemPayload)
            || Runtime::ReadUInt16LittleEndian(itemPayload) != matchId)
        {
            return std::nullopt;
        }

        std::uint32_t occupied = 0;
        for (std::size_t i = 0; i < header.PlayerCount; ++i)
        {
            const std::size_t playerOffset = SnapshotHeader::Size + i * PlayerState::Size;
            const PlayerState state = PlayerState::Read(payload.subspan(playerOffset, PlayerState::Size));
            const std::size_t slot = state.SlotIndex;
            if (!state.StatusEffectFlagsMatchTimers()
                || !slotIdentity.IsValidSlot(slot) || (occupied & (1U << slot)) != 0
                || !slotIdentity.MatchesGeneration(slot, state.SlotGeneration))
            {
                return std::nullopt;
            }

            occupied |= 1U << slot;
            validated.SlotLives[i] = { state.SlotIndex, state.LifeId };
        }
        validated.SlotCount = header.PlayerCount;
        return validated;
    }
}
