#include "Mods/Network/Session/SessionChatChannel.hpp"
#include "Mods/Network/Session/PeerRegistry.hpp"
#include "Mods/Network/Session/SessionRoster.hpp"
#include "Mods/Chat/NetChat.hpp"
#include "GameState.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::StringIsNullOrWhiteSpace;

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] std::span<const std::uint8_t> First(
            const std::array<std::uint8_t, NetConfig::MaxPacketSize>& buffer, std::size_t count)
        {
            return std::span<const std::uint8_t>(buffer).first(count);
        }
    }

    void SessionChatChannel::Receive(ReceivedPacket packet, double time, SessionConnection& connection,
        const SessionRuntimeState& runtime)
    {
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < ChatPacket::Size)
        {
            return;
        }
        ChatPacket chat = ChatPacket::Read(payload);
        if (RequireReference(chat.Text).empty())
        {
            return;
        }
        if (runtime.Role() == NetRole::Host)
        {
            std::shared_ptr<PeerRegistry::Peer> peer = SessionPeers().FindByEndpoint(packet.Sender);
            if (peer == nullptr || peer->SlotIndex() < 0)
            {
                return;
            }
            peer->MarkSeen(time);
            chat.Slot = static_cast<std::uint8_t>(peer->SlotIndex());
            if (peer->SlotIndex() < static_cast<std::int32_t>(GameState::Nicknames().size()))
            {
                const std::optional<std::string> nickname = GameState::Nicknames()[peer->SlotIndex()];
                if (nickname.has_value() && !nickname->empty())
                {
                    chat.Name = nickname;
                }
            }
            const auto team = [](std::int32_t slot) { return SessionSlots().TeamAt(slot); };
            const bool teamOnly = chat.Kind == ChatPacket::KindTeam && GameState::Teams() && team(peer->SlotIndex()) >= 0;
            chat.Kind = teamOnly ? ChatPacket::KindTeam : ChatPacket::KindSay;
            chat.Write(connection.Buffer());
            for (const std::shared_ptr<PeerRegistry::Peer>& other : SessionPeers().Peers())
            {
                if (other != peer && (!teamOnly || team(other->SlotIndex()) == team(peer->SlotIndex())) && connection.IsOpen())
                {
                    connection.Send(other->Endpoint(), PacketType::Chat, First(connection.Buffer(), ChatPacket::Size));
                }
            }
            if (teamOnly && (runtime.LocalSlot() < 0 || team(runtime.LocalSlot()) != team(peer->SlotIndex())))
            {
                return;
            }
        }
        Mods::Chat::NetChat::Receive(chat);
    }

    void SessionChatChannel::Send(const std::string& value, SessionConnection& connection, NetRole role,
        std::int32_t localSlot, const std::string& playerName, ActiveMatchProvider activeMatch)
    {
        if (!connection.IsOpen() || StringIsNullOrWhiteSpace(value))
        {
            return;
        }
        std::string text = value;
        bool teamOnly = Runtime::StringStartsWithOrdinalIgnoreCase(text, "/team ");
        if (teamOnly)
        {
            text = Runtime::StringTrim(text.substr(6));
        }
        if (text.empty())
        {
            return;
        }
        const std::optional<MatchDefinition> match = activeMatch();
        teamOnly = teamOnly && match.has_value() && GameState::IsTeamMode(match->Mode);
        ChatPacket chat{};
        chat.Slot = static_cast<std::uint8_t>(std::max(localSlot, 0));
        chat.Kind = teamOnly ? ChatPacket::KindTeam : ChatPacket::KindSay;
        chat.Name = playerName;
        chat.Text = text;
        Mods::Chat::NetChat::Remember(chat);
        chat.Write(connection.Buffer());
        if (role == NetRole::Host)
        {
            for (const std::shared_ptr<PeerRegistry::Peer>& peer : SessionPeers().Peers())
            {
                if (teamOnly && SessionSlots().TeamAt(peer->SlotIndex())
                    != SessionSlots().TeamAt(std::max(localSlot, 0)))
                {
                    continue;
                }
                connection.Send(peer->Endpoint(), PacketType::Chat, First(connection.Buffer(), ChatPacket::Size));
            }
            return;
        }
        if (connection.HasHostEndpoint())
        {
            connection.SendToHost(PacketType::Chat, First(connection.Buffer(), ChatPacket::Size));
        }
    }

}
