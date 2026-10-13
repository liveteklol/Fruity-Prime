#include "Mods/Network/Server/Core/ServerPeerIdentity.hpp"

#include "Mods/Launcher/Portable/LaunchPlan.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"
#include "NativeRuntime/System/Encoding.hpp"
#include "NativeRuntime/System/Globalization.hpp"

namespace MphRead::Mods::Network
{
    std::optional<ServerPlayerIdentity> ServerPeerIdentity::Parse(std::span<const std::uint8_t> payload)
    {
        if (payload.size() < 2)
        {
            return std::nullopt;
        }

        ServerPlayerIdentity identity{};
        identity.Hunter = payload[0];
        identity.Color = payload[1];
        if (identity.Hunter >= Launcher::Hunters::Playable || identity.Color > 3)
        {
            return std::nullopt;
        }

        identity.Name = ::MphRead::NativeRuntime::AsciiGetString(payload.subspan(2));
        while (!identity.Name.empty() && identity.Name.back() == '\0')
        {
            identity.Name.pop_back();
        }
        identity.Name = ::MphRead::NativeRuntime::StringTrim(identity.Name);
        if (identity.Name.empty())
        {
            return std::nullopt;
        }
        if (identity.Name.size() > RosterPacket::MaxNameBytes)
        {
            identity.Name.resize(RosterPacket::MaxNameBytes);
        }
        return identity;
    }
}
