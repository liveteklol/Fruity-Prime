#pragma once

#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Builds the current player-state portion of an intent. It has no session
    // or local-input history and can be shared by human and server-bot paths.
    class NetPlayerIntent final
    {
    public:
        NetPlayerIntent() = delete;

        [[nodiscard]] static IntentPacket CaptureState(Entities::PlayerEntity& player);
    };
}
