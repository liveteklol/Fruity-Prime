#include "Mods/Network/Session/SessionPeerHandshake.hpp"

#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Exceptions.hpp"

#include <cstdint>
#include <memory>
#include <span>

using ::MphRead::NativeRuntime::ConsoleWriteLine;

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] std::span<std::uint8_t> Scratch(
            std::array<std::uint8_t, NetConfig::MaxPacketSize>& buffer, std::size_t start)
        {
            return std::span<std::uint8_t>(buffer).subspan(start);
        }

        [[nodiscard]] std::span<const std::uint8_t> First(
            const std::array<std::uint8_t, NetConfig::MaxPacketSize>& buffer, std::size_t count)
        {
            return std::span<const std::uint8_t>(buffer).first(count);
        }
    }

    bool SessionPeerHandshake::HandleHello(ReceivedPacket packet, double time,
        SessionConnection& connection, const SessionMatchIdentity& matchIdentity,
        OccupantWriter setOccupant, OccupantGenerationReader occupantGeneration)
    {
        if (setOccupant == nullptr || occupantGeneration == nullptr)
        {
            return false;
        }
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < 1 || payload[0] != NetConfig::ProtocolVersion)
        {
            return false;
        }
        const std::uint32_t clientId = payload.size() >= 6
            ? Runtime::ReadUInt32LittleEndian(payload.subspan(2, 4))
            : 0U;
        std::shared_ptr<PeerRegistry::Peer> peer = SessionPeers().FindByEndpoint(packet.Sender);
        if (peer != nullptr && peer->ClientId() != clientId)
        {
            SessionPeers().Remove(peer);
            setOccupant(peer->SlotIndex(), 0);
            peer = nullptr;
        }
        if (peer == nullptr && clientId != 0)
        {
            peer = SessionPeers().FindByClientId(clientId);
            if (peer != nullptr)
            {
                ConsoleWriteLine("[net] slot " + Runtime::ToString(peer->SlotIndex())
                    + " came back on " + packet.Sender->ToString()
                    + " (was " + peer->Endpoint()->ToString() + ")");
                peer->Rebind(packet.Sender);
            }
        }
        if (peer == nullptr)
        {
            const std::int32_t slot = SessionPeers().NextFreeSlot(1, Entities::PlayerEntity::MaxPlayers());
            if (slot < 0)
            {
                return false;
            }
            peer = SessionPeers().AddPeer(packet.Sender, slot, clientId, time);
            const std::uint16_t generation = NetLifecycleTracker::Next(
                SessionPeers().HostGeneration(static_cast<std::size_t>(slot)));
            SessionPeers().SetHostGeneration(static_cast<std::size_t>(slot), generation);
            setOccupant(slot, generation);
            ConsoleWriteLine("[net] peer " + packet.Sender->ToString()
                + " -> slot " + Runtime::ToString(slot));
        }
        peer->ObserveClient(clientId, time);
        connection.Buffer()[0] = static_cast<std::uint8_t>(peer->SlotIndex());
        Runtime::WriteUInt32LittleEndian(Scratch(connection.Buffer(), 1), clientId);
        Runtime::WriteUInt16LittleEndian(Scratch(connection.Buffer(), 5), matchIdentity.MatchId(true));
        Runtime::WriteUInt64LittleEndian(Scratch(connection.Buffer(), 7), matchIdentity.AuthorityEpoch(true));
        Runtime::WriteUInt16LittleEndian(Scratch(connection.Buffer(), 15), occupantGeneration(peer->SlotIndex()));
        if (!connection.IsOpen())
        {
            throw System::NullReferenceException();
        }
        connection.Send(peer->Endpoint(), PacketType::Welcome, First(connection.Buffer(), 17));
        return true;
    }
}
