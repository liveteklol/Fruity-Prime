#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Defines which network player roles use replica-only placement and draw policies.
    class ReplicaPlayerPolicy final
    {
    public:
        struct Context final
        {
            bool isClient = false;
            bool isHost = false;
            bool isAuthority = false;
            std::int32_t slot = -1;
            std::int32_t localSlot = -1;
            bool isBot = false;
        };

        ReplicaPlayerPolicy() = delete;

        [[nodiscard]] static bool UsesReplicaRoomPlacement(const Context& context) noexcept
        {
            return context.isClient && !context.isAuthority && IsRemoteHuman(context);
        }

        [[nodiscard]] static bool UsesReplicaShadowVisibility(const Context& context) noexcept
        {
            return (context.isClient || context.isHost) && IsRemoteHuman(context);
        }

        [[nodiscard]] static bool IsLocalSlot(
            std::int32_t slot, std::int32_t localSlot) noexcept
        {
            return slot == localSlot;
        }

    private:
        [[nodiscard]] static bool IsRemoteHuman(const Context& context) noexcept
        {
            return !IsLocalSlot(context.slot, context.localSlot) && !context.isBot;
        }
    };
}
