#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Who settles that a player took an item.
    enum class PickupRoute : std::uint8_t
    {
        // This machine, as the game always has: offline, an item no spawner
        // put out (a death's ammo drop), or one nothing is synced for.
        Local,
        // This machine is the authority: it grants and records the pickup.
        Authority,
        // A client's own player touched a power-up: show it now, and claim it
        // (NetPowerUpPrediction, NetPickupClaims).
        Claim,
        // A client, for an item only the authority hands out: a remote
        // player's contact, or health, which is not predicted.
        None
    };

    struct PickupContext
    {
        bool Networked = false;
        bool Authority = false;
        // Put out by a spawner the item sync knows (NetItemState).
        bool Spawned = false;
        // Health or a power-up: the authority's to hand out.
        bool ServerOwned = false;
        bool PowerUp = false;
        // The player touching it is this machine's own.
        bool OwnPlayer = false;
    };

    [[nodiscard]] constexpr PickupRoute RouteFor(const PickupContext& context) noexcept
    {
        if (!context.Networked || !context.Spawned || !context.ServerOwned)
        {
            return PickupRoute::Local;
        }
        if (context.Authority)
        {
            return PickupRoute::Authority;
        }
        return context.PowerUp && context.OwnPlayer ? PickupRoute::Claim : PickupRoute::None;
    }
}
