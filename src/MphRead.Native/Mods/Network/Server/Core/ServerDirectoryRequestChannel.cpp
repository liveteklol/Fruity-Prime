#include "Mods/Network/Server/Core/ServerDirectoryRequestChannel.hpp"

#include "Mods/Network/Server/Core/HostPool.hpp"
#include "Mods/Network/Server/Core/ServerStateBroadcastChannel.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    ServerDirectoryRequestChannel::ServerDirectoryRequestChannel(Ports ports) noexcept
        : _ports(ports)
    {
    }

    MatchDefinition ServerDirectoryRequestChannel::CurrentDefinition() const
    {
        return _ports.CurrentDefinition != nullptr
            ? _ports.CurrentDefinition(_ports.Context) : MatchDefinition{};
    }

    void ServerDirectoryRequestChannel::Log(const std::string& message) const
    {
        if (_ports.WriteLog != nullptr)
        {
            _ports.WriteLog(_ports.Context, message);
        }
    }

    void ServerDirectoryRequestChannel::SendStatus(
        const std::shared_ptr<System::Net::IPEndPoint>& sender, double now)
    {
        const MatchDefinition definition = CurrentDefinition();
        ServerStatusPacket status{};
        status.Match = _ports.State->BuildMatchState(now);
        status.Phase = _ports.Lobby->Phase();
        status.Format = definition.Format;
        status.LobbyEnabled = _ports.Lobby->Policy() == ServerSessionPolicy::Lobby;
        status.AllowJoinInProgress = _ports.Lobby->AllowJoinInProgress();
        status.MaxPlayers = static_cast<std::uint8_t>(_ports.MaxPlayers);
        status.Protocol = static_cast<std::uint8_t>(NetConfig::ProtocolVersion);
        status.ServerName = *_ports.ServerName;
        status.Flags = static_cast<std::uint8_t>(_ports.Hosts->CanHost()
            ? ServerStatusPacket::FlagCanHost : 0);
        status.Write(*_ports.Scratch);
        if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
        {
            (*_ports.Transport)->Send(sender, PacketType::StatusReply,
                std::span<const std::uint8_t>(*_ports.Scratch).first(ServerStatusPacket::SizeWithFlags));
        }
    }

    void ServerDirectoryRequestChannel::HandleHostRequest(const ReceivedPacket& packet, double now)
    {
        HostReplyPacket reply{};
        const std::span<const std::uint8_t> payload = packet.Payload();
        if (payload.size() < static_cast<std::size_t>(HostRequestPacket::Size))
        {
            reply.Reason = "malformed request";
        }
        else
        {
            const HostRequestPacket request = HostRequestPacket::Read(payload);
            if (request.Protocol != NetConfig::ProtocolVersion)
            {
                reply.Reason = "this server speaks protocol " + std::to_string(NetConfig::ProtocolVersion)
                    + ", your build speaks " + std::to_string(request.Protocol);
            }
            else if (!_ports.Hosts->CanHost())
            {
                reply.Reason = "this server does not open new games";
            }
            else
            {
                reply = _ports.Hosts->Start(request, packet.Sender, now);
            }
        }
        reply.Write(*_ports.Scratch);
        if (_ports.Transport != nullptr && *_ports.Transport != nullptr)
        {
            (*_ports.Transport)->Send(packet.Sender, PacketType::HostReply,
                std::span<const std::uint8_t>(*_ports.Scratch).first(HostReplyPacket::Size));
        }
        if (!reply.Started)
        {
            Log("refused a game for " + packet.Sender->ToString() + ": " + reply.Reason.value_or(""));
        }
    }
}
