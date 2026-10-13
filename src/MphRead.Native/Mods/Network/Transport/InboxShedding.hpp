#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <cstddef>
#include <optional>

namespace MphRead::Mods::Network
{
    // A packet whose loss the next one of its kind makes good: a stream sent
    // every frame (an intent repeats its recent shots and bombs, a snapshot
    // its recent damage), or a measurement that is simply taken again. Every
    // other packet -- a welcome, a map change, a match state, a hit verdict --
    // is said once, and losing it to a full queue loses it for good.
    [[nodiscard]] constexpr bool Superseded(PacketType type) noexcept
    {
        switch (type)
        {
        case PacketType::Intent:
        case PacketType::SlotIntent:
        case PacketType::Snapshot:
        case PacketType::Ping:
        case PacketType::Pong:
        case PacketType::StatusQuery:
        case PacketType::StatusReply:
        case PacketType::MasterHeartbeat:
            return true;
        default:
            return false;
        }
    }

    // Which queued packet a newer one can replace: the oldest superseded
    // stream item. Important one-shot packets are never implicit victims.
    template <typename Queue, typename TypeOf>
    [[nodiscard]] std::optional<std::size_t> ShedIndex(const Queue& queue, TypeOf typeOf)
    {
        std::size_t index = 0;
        for (const auto& packet : queue)
        {
            if (Superseded(typeOf(packet)))
            {
                return index;
            }
            index++;
        }
        return std::nullopt;
    }
}
