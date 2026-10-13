#include "Mods/Network/Server/Match/ServerSnapshotValidator.hpp"
#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Session/NetMatchTimeSync.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    using namespace MphRead::Mods::Network;

    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }

    std::vector<std::uint8_t> MakeSnapshot(std::uint16_t matchId, std::uint64_t authorityEpoch,
        std::span<const PlayerState> players)
    {
        const std::size_t timeOffset = SnapshotHeader::Size + players.size() * PlayerState::Size;
        std::vector<std::uint8_t> payload(timeOffset + NetMatchTimeSync::Size + NetItemState::HeaderSize);

        SnapshotHeader header{};
        header.MatchId = matchId;
        header.AuthorityEpoch = authorityEpoch;
        header.Frame = 7;
        header.PlayerCount = static_cast<std::uint8_t>(players.size());
        header.Write(payload);
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            players[i].Write(std::span<std::uint8_t>(payload).subspan(
                SnapshotHeader::Size + i * PlayerState::Size, PlayerState::Size));
        }

        const std::size_t itemOffset = timeOffset + NetMatchTimeSync::Size;
        payload[itemOffset] = static_cast<std::uint8_t>(matchId & 0xFF);
        payload[itemOffset + 1] = static_cast<std::uint8_t>(matchId >> 8);
        payload[itemOffset + 2] = 0;
        return payload;
    }
}

int main()
{
    constexpr std::uint16_t MatchId = 23;
    constexpr std::uint64_t AuthorityEpoch = 51;
    constexpr std::size_t Slot = 3;

    ServerSlotIdentity identities;
    identities.EnsureGeneration(Slot);

    PlayerState player{};
    player.SlotIndex = static_cast<std::uint8_t>(Slot);
    player.SlotGeneration = identities.Generation(Slot);
    player.LifeId = 9;
    const std::array<PlayerState, 1> players{ player };
    std::vector<std::uint8_t> payload = MakeSnapshot(MatchId, AuthorityEpoch, players);

    bool passed = true;
    const auto validated = ServerSnapshotValidator::Validate(payload, MatchId, AuthorityEpoch, identities);
    passed &= Check(validated.has_value(), "valid authority snapshot is accepted");
    if (validated)
    {
        passed &= Check(validated->Header.Frame == 7 && validated->SlotCount == 1
            && validated->SlotLives[0].SlotIndex == Slot && validated->SlotLives[0].LifeId == 9,
            "validated snapshot returns frame and bounded slot-life updates");
    }

    PlayerState inconsistentEffects = player;
    inconsistentEffects.StatusEffects.BurningFrames = 1;
    const std::array<PlayerState, 1> inconsistentEffectPlayers{ inconsistentEffects };
    passed &= Check(!ServerSnapshotValidator::Validate(
        MakeSnapshot(MatchId, AuthorityEpoch, inconsistentEffectPlayers), MatchId, AuthorityEpoch, identities),
        "timed-effect flags and authoritative remaining timers must agree");

    passed &= Check(!ServerSnapshotValidator::Validate(
        std::span<const std::uint8_t>(payload).first(SnapshotHeader::Size - 1),
        MatchId, AuthorityEpoch, identities), "truncated snapshot header is rejected");
    passed &= Check(!ServerSnapshotValidator::Validate(payload, MatchId + 1, AuthorityEpoch, identities),
        "cross-match snapshot is rejected");
    passed &= Check(!ServerSnapshotValidator::Validate(payload, MatchId, AuthorityEpoch + 1, identities),
        "stale authority epoch is rejected");

    std::vector<std::uint8_t> wrongItemMatch = payload;
    const std::size_t itemOffset = SnapshotHeader::Size + PlayerState::Size + NetMatchTimeSync::Size;
    wrongItemMatch[itemOffset] = static_cast<std::uint8_t>((MatchId + 1) & 0xFF);
    wrongItemMatch[itemOffset + 1] = static_cast<std::uint8_t>((MatchId + 1) >> 8);
    passed &= Check(!ServerSnapshotValidator::Validate(wrongItemMatch, MatchId, AuthorityEpoch, identities),
        "cross-match item state is rejected");

    std::vector<std::uint8_t> nonFiniteTime = payload;
    const std::size_t timeOffset = SnapshotHeader::Size + PlayerState::Size;
    nonFiniteTime[timeOffset + 2] = 0xC0;
    nonFiniteTime[timeOffset + 3] = 0x7F;
    passed &= Check(!ServerSnapshotValidator::Validate(nonFiniteTime, MatchId, AuthorityEpoch, identities),
        "non-finite match time is rejected");

    const std::array<PlayerState, 2> duplicateSlots{ player, player };
    passed &= Check(!ServerSnapshotValidator::Validate(
        MakeSnapshot(MatchId, AuthorityEpoch, duplicateSlots), MatchId, AuthorityEpoch, identities),
        "duplicate slot identity is rejected");

    PlayerState invalidSlot = player;
    invalidSlot.SlotIndex = static_cast<std::uint8_t>(ServerSlotIdentity::SlotCapacity);
    const std::array<PlayerState, 1> invalidSlotPlayers{ invalidSlot };
    passed &= Check(!ServerSnapshotValidator::Validate(
        MakeSnapshot(MatchId, AuthorityEpoch, invalidSlotPlayers), MatchId, AuthorityEpoch, identities),
        "out-of-range slot is rejected");

    PlayerState staleGeneration = player;
    staleGeneration.SlotGeneration = static_cast<std::uint16_t>(player.SlotGeneration + 1);
    const std::array<PlayerState, 1> staleGenerationPlayers{ staleGeneration };
    passed &= Check(!ServerSnapshotValidator::Validate(
        MakeSnapshot(MatchId, AuthorityEpoch, staleGenerationPlayers), MatchId, AuthorityEpoch, identities),
        "stale slot generation is rejected");

    std::vector<std::uint8_t> malformedItems = payload;
    malformedItems.back() = 1;
    passed &= Check(!ServerSnapshotValidator::Validate(malformedItems, MatchId, AuthorityEpoch, identities),
        "trailing malformed item state is rejected");

    std::vector<PlayerState> tooManyPlayers(ServerSlotIdentity::SlotCapacity + 1, player);
    passed &= Check(!ServerSnapshotValidator::Validate(
        MakeSnapshot(MatchId, AuthorityEpoch, tooManyPlayers), MatchId, AuthorityEpoch, identities),
        "player count above wire slot capacity is rejected");

    return passed ? 0 : 1;
}
