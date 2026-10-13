#pragma once

#include <cstdint>
#include <optional>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Which players on this machine the game's own AI plays in a networked
    // match: a dedicated server's bot slots (ServerBots), and this client's
    // own player when it runs as an AI test client (-netcheck -ai) -- one
    // more person on the wire, over a real line, whose hands are the AI's.
    // Everybody else is a person's copy, AI off.
    class NetBotRoles final
    {
    public:
        NetBotRoles() = delete;

        // This client's own player is played by the AI at `level` (0-3).
        static void PlayLocalAs(std::optional<std::int32_t> level) noexcept;
        [[nodiscard]] static bool LocalIsAi() noexcept { return _localLevel.has_value(); }
        // Sets a player's role. Called wherever a network slot is set up,
        // in place of switching the AI off.
        static void Assign(Entities::PlayerEntity& player);
        // A bot spawning before its AI was loaded -- it became one after its
        // room loaded, which is when the game loads every bot's AI -- gets
        // it now, as that load would have given it.
        static void PrepareSpawn(Entities::PlayerEntity& player);

    private:
        [[nodiscard]] static std::optional<std::int32_t> LevelFor(std::int32_t slot) noexcept;

        inline static std::optional<std::int32_t> _localLevel{};
    };
}
