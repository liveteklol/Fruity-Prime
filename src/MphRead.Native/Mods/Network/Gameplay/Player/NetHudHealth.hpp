#pragma once

#include <cstdint>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    struct HudHealthSample final
    {
        std::int32_t Health = 0;
        std::uint32_t SnapshotFrame = 0;
        std::uint16_t LifeId = 0;
        bool Authoritative = false;

        [[nodiscard]] friend bool operator==(const HudHealthSample&, const HudHealthSample&) = default;
    };

    class NetHudHealth final
    {
    public:
        NetHudHealth() = delete;

        [[nodiscard]] static bool HideOpponents();
        // A followed player is still somebody else's player. Playback has no
        // privileged HP view.
        [[nodiscard]] static bool Visible(std::int32_t slot);
        [[nodiscard]] static HudHealthSample Sample(const ::MphRead::Entities::PlayerEntity& player);
    };
}
