#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Formats/Enums.hpp"

#include <cstdint>
#include <string>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // AI players a dedicated server runs in slots of its own (-bots N): the
    // game's own bots, playing on the authority, seen by every client as one
    // more remote player. The top N slots are theirs and no person is given
    // one. Each frame the server tells every client what each bot did, as
    // the intent a client of its own would have sent (IntentFor), so a bot
    // moves, turns and fires on every screen the way a person's copy does.
    // Hits by and on a bot are the authority's to resolve: a bot has no
    // machine of its own to claim them.
    class ServerBots final
    {
    public:
        ServerBots() = delete;

        static constexpr std::int32_t DefaultLevel = 2;
        static constexpr std::int32_t MaxLevel = 3;

        // At most `maxPlayers - 1`: a person always has room.
        static void Configure(std::int32_t count, std::int32_t level, std::int32_t maxPlayers) noexcept;
        [[nodiscard]] static std::int32_t Count() noexcept { return _count; }
        [[nodiscard]] static std::int32_t Level() noexcept { return _level; }
        // A slot this server's AI plays.
        [[nodiscard]] static bool Holds(std::int32_t slot) noexcept;
        [[nodiscard]] static ::MphRead::Hunter HunterFor(std::int32_t slot) noexcept;
        [[nodiscard]] static std::string NameFor(std::int32_t slot);
        // What every client is told the bot did this frame.
        [[nodiscard]] static IntentPacket IntentFor(Entities::PlayerEntity& player,
            std::uint32_t frame, std::uint16_t matchId, std::uint64_t authorityEpoch);

    private:
        inline static std::int32_t _count = 0;
        inline static std::int32_t _level = DefaultLevel;
        inline static std::int32_t _first = 0;
        inline static std::int32_t _maxPlayers = 0;
    };
}
