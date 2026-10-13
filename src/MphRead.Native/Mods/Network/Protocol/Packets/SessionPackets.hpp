#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    struct MatchStatePacket
    {
        std::uint64_t AuthorityEpoch = 0;
        static constexpr std::int32_t MaxNameBytes = 40;
        static constexpr std::int32_t Size = 1 + 4 + 4 + 1 + 1 + 2 + 2 + MaxNameBytes + MaxNameBytes + 8;

        std::uint8_t Mode = 0;
        float TimeRemaining = 0.0F;
        float TimeElapsed = 0.0F;
        std::uint8_t PlayerCount = 0;
        std::uint8_t Flags = 0;
        std::uint16_t PointGoal = 0;
        std::uint16_t MatchId = 0;
        std::optional<std::string> RoomKey{};
        std::optional<std::string> NextRoomKey{};

        static constexpr std::uint8_t FlagInProgress = 1U << 0;
        static constexpr std::uint8_t FlagEnding = 1U << 1;
        static constexpr std::uint8_t FlagFriendlyFire = 1U << 2;
        static constexpr std::uint8_t FlagNoShadowFreeze = 1U << 3;
        // Bits 4-5: the damage level every machine in this match scales its
        // hits by, as the level plus one, so that zero means "this server did
        // not say".
        static constexpr std::uint8_t FlagDamageShift = 4;
        static constexpr std::uint8_t FlagDamageMask = 0b11U << FlagDamageShift;
        // Bit 6: weapon pickups are the picking hunter's affinity variant.
        static constexpr std::uint8_t FlagAffinityWeapons = 1U << 6;

        [[nodiscard]] bool Ending() const noexcept;
        [[nodiscard]] bool FriendlyFire() const noexcept;
        [[nodiscard]] bool ShadowFreeze() const noexcept;
        // The damage level this server plays at, or -1 when it did not say.
        [[nodiscard]] std::int32_t DamageLevel() const noexcept;
        // Whether this server states its damage rules at all.
        [[nodiscard]] bool StatesRules() const noexcept;
        [[nodiscard]] bool AffinityWeapons() const noexcept;
        // Pack the two rules into the spare bits of the flags byte. A level
        // outside 0-2 is "do not say", which is what an older server sends.
        [[nodiscard]] static std::uint8_t RuleFlags(
            std::int32_t damageLevel, bool affinityWeapons) noexcept;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static MatchStatePacket Read(std::span<const std::uint8_t> src);

    private:
        static void WriteName(std::span<std::uint8_t> dest,
            const std::optional<std::string>& value);
        [[nodiscard]] static std::string ReadName(std::span<const std::uint8_t> src);
    };

    struct ServerStatusPacket
    {
        static constexpr std::int32_t MaxNameBytes = 32;
        static constexpr std::int32_t Size = MatchStatePacket::Size + 2 + MaxNameBytes;
        // The same packet with five bytes of capability and session on the
        // end, after the name so that the length check separates old from new.
        static constexpr std::int32_t SizeWithFlags = Size + 5;
        // Bit 0: this server will open a new match on a port of its own.
        static constexpr std::uint8_t FlagCanHost = 1;

        SessionPhase Phase = SessionPhase::Lobby;
        MatchFormat Format = MatchFormat::Auto;
        bool LobbyEnabled = false;
        bool AllowJoinInProgress = false;
        MatchStatePacket Match{};
        std::uint8_t MaxPlayers = 0;
        std::uint8_t Protocol = 0;
        // What this server can do beyond running the match it is running.
        // Zero for a server that did not say, which reads as "cannot".
        std::uint8_t Flags = 0;
        std::optional<std::string> ServerName{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static ServerStatusPacket Read(std::span<const std::uint8_t> src);
    };

    struct RosterPacket
    {
        static constexpr std::int32_t MaxNameBytes = 16;
        static constexpr std::int32_t MaxSlots = static_cast<std::int32_t>(NetworkSlotCapacity);
        static constexpr std::int32_t EntrySize = 1 + 1 + 1 + 2 + MaxNameBytes + 4;
        static constexpr std::int32_t HeaderSize = 17;
        static constexpr std::int32_t Size = HeaderSize + MaxSlots * EntrySize;
        std::uint16_t SessionRevision = 0;
        std::uint16_t MatchId = 0;
        std::uint64_t AuthorityEpoch = 0;
        std::uint32_t Revision = 0;
        std::shared_ptr<std::vector<std::uint16_t>> Generations{};

        std::uint8_t Count = 0;
        std::shared_ptr<std::vector<std::int8_t>> Teams{};
        std::shared_ptr<std::vector<bool>> LobbyReady{};
        std::shared_ptr<std::vector<std::uint8_t>> Slots{};
        std::shared_ptr<std::vector<std::uint8_t>> Hunters{};
        std::shared_ptr<std::vector<std::uint8_t>> Colors{};
        std::shared_ptr<std::vector<std::uint16_t>> Pings{};
        std::shared_ptr<std::vector<std::optional<std::string>>> Names{};

        [[nodiscard]] static RosterPacket Create();
        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static bool TryRead(std::span<const std::uint8_t> src, RosterPacket& roster);
        [[nodiscard]] static RosterPacket Read(std::span<const std::uint8_t> src);

    private:
        static void WriteName(std::span<std::uint8_t> dest,
            const std::optional<std::string>& value);
        [[nodiscard]] static std::string ReadName(std::span<const std::uint8_t> src);
    };

}
