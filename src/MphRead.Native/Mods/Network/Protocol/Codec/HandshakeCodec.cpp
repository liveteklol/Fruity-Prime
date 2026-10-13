#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"
#include "Mods/Network/Protocol/Packets/HandshakePackets.hpp"

namespace MphRead::Mods::Network
{
    using namespace Protocol::Wire;

    void HelloPacket::Write(std::span<std::uint8_t> dest) const
    {
        if (dest.size() < static_cast<std::size_t>(Size))
        {
            throw std::out_of_range("dest too small for a hello");
        }
        dest[0] = Protocol;
        dest[1] = Slot;
        W32(dest.subspan(2, 4), ClientId);
        static_cast<void>(OwnerToken.TryWriteBytes(dest.subspan(6, 16)));
        std::copy(Secret.begin(), Secret.end(), dest.begin() + 22);
    }

    HelloPacket HelloPacket::Read(std::span<const std::uint8_t> src)
    {
        HelloPacket hello{};
        if (src.size() >= 1)
        {
            hello.Protocol = src[0];
        }
        if (src.size() >= 2)
        {
            hello.Slot = src[1];
        }
        if (src.size() >= 6)
        {
            hello.ClientId = R32(src.subspan(2, 4));
        }
        if (src.size() >= 22)
        {
            hello.OwnerToken = ::MphRead::NativeRuntime::Guid(src.subspan(6, 16));
        }
        if (src.size() >= static_cast<std::size_t>(Size))
        {
            std::copy_n(src.begin() + 22, hello.Secret.size(), hello.Secret.begin());
        }
        return hello;
    }

    void RefusedPacket::Write(std::span<std::uint8_t> dest) const
    {
        At(dest, 0) = Reason;
        At(dest, 1) = Players;
        At(dest, 2) = MaxPlayers;
    }
    RefusedPacket RefusedPacket::Read(std::span<const std::uint8_t> src)
    {
        RefusedPacket packet;
        packet.Reason = At(src, 0);
        packet.Players = src.size() > 1 ? src[1] : static_cast<std::uint8_t>(0);
        packet.MaxPlayers = src.size() > 2 ? src[2] : static_cast<std::uint8_t>(0);
        return packet;
    }
    std::string RefusedPacket::Describe(const std::optional<std::string> &where) const
    {
        const std::string prefix = where.value_or("");
        if (Reason == ReasonKicked)
        {
            return "You were removed by the lobby owner.";
        }
        if (Reason == ReasonInMatch)
        {
            return "This server does not allow joining a match in progress.";
        }
        if (Reason == ReasonIdentity)
        {
            return prefix + " already has this connection on another address. Restart the game to join again.";
        }
        if (Reason == ReasonFull)
        {
            return prefix + " is full (" + std::to_string(static_cast<unsigned int>(Players)) + "/" + std::to_string(static_cast<unsigned int>(MaxPlayers)) + " players). Try again when somebody leaves.";
        }
        if (Reason == ReasonProtocol)
        {
            return prefix + " is running a different version of the game. " + "One of you needs updating.";
        }
        return prefix + " would not admit this client.";
    }
}
