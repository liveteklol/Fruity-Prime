#include "Mods/Network/Server/Core/ServerChatChannel.hpp"

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    namespace
    {
        void WriteSystemMessage(std::span<std::uint8_t> scratch, const std::string& text)
        {
            ChatPacket chat{};
            chat.Slot = 0xFF;
            chat.Kind = ChatPacket::KindSystem;
            chat.Name = std::string{};
            chat.Text = text;
            chat.Write(scratch);
        }
    }

    void ServerChatChannel::Tell(NetTransport* transport, std::span<std::uint8_t> scratch,
        const PeerHandle& peer, const std::string& text)
    {
        WriteSystemMessage(scratch, text);
        if (transport != nullptr)
        {
            transport->Send(peer->EndPoint, PacketType::Chat,
                std::span<const std::uint8_t>(scratch).first(ChatPacket::Size));
        }
    }

    void ServerChatChannel::Receive(const ReceivedPacket& packet, double now,
        ServerPeerRegistry& peers, NetTransport* transport,
        std::span<std::uint8_t> scratch, bool teamMode, LogSink log)
    {
        const ServerPeerRegistry::PeerHandle peer = peers.Find(packet.Sender);
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (peer == nullptr || peer->SlotIndex < 0
            || payload.size() < static_cast<std::size_t>(ChatPacket::Size))
        {
            return;
        }
        peer->Liveness.MarkSeen(now);
        ChatPacket chat = ChatPacket::Read(payload);
        const std::string text = chat.Text.value_or(std::string{});
        if (text.empty())
        {
            return;
        }
        const PeerChatRateLimiter::Decision decision = peer->ChatRate.TryConsume(now);
        if (decision != PeerChatRateLimiter::Decision::Allowed)
        {
            if (decision == PeerChatRateLimiter::Decision::FirstSuppressed && log != nullptr)
            {
                log("chat from slot " + std::to_string(peer->SlotIndex) + " ("
                    + peer->EndPoint->ToString() + ") dropped: too fast");
            }
            return;
        }
        chat.Slot = static_cast<std::uint8_t>(peer->SlotIndex);
        chat.Name = !peer->Profile.Name().empty()
            ? peer->Profile.Name() : "Player" + std::to_string(peer->SlotIndex);
        const bool teamOnly = chat.Kind == ChatPacket::KindTeam && teamMode
            && peer->Lobby.TeamIndex() >= 0;
        chat.Kind = teamOnly ? ChatPacket::KindTeam : ChatPacket::KindSay;
        chat.Write(scratch);
        if (transport != nullptr)
        {
            for (const ServerPeerRegistry::PeerHandle& other : peers.All())
            {
                if (other != peer && (!teamOnly || other->Lobby.TeamIndex() == peer->Lobby.TeamIndex()))
                {
                    transport->Send(other->EndPoint, PacketType::Chat,
                        std::span<const std::uint8_t>(scratch).first(ChatPacket::Size));
                }
            }
        }
        if (log != nullptr)
        {
            log("chat " + chat.Name.value_or(std::string{}) + ": " + text);
        }
    }

    void ServerChatChannel::Announce(ServerPeerRegistry& peers, NetTransport* transport,
        std::span<std::uint8_t> scratch, const std::string& text)
    {
        if (peers.All().empty())
        {
            return;
        }
        WriteSystemMessage(scratch, text);
        if (transport != nullptr)
        {
            for (const ServerPeerRegistry::PeerHandle& peer : peers.All())
            {
                transport->Send(peer->EndPoint, PacketType::Chat,
                    std::span<const std::uint8_t>(scratch).first(ChatPacket::Size));
            }
        }
    }
}
