#include "NetChat.hpp"

#include "ChatBox.hpp"

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

namespace MphRead::Mods::Chat
{
    using Network::ChatPacket;
    using ::MphRead::NativeRuntime::UncheckedAdd;

    std::vector<std::string> NetChat::_history{};
    std::vector<std::pair<std::string, bool>> NetChat::_entries{};
    std::int32_t NetChat::_revision = 0;

    void NetChat::Clear()
    {
        _history.clear();
        _entries.clear();
        _revision = UncheckedAdd(_revision, 1);
    }

    void NetChat::Receive(const ChatPacket& packet)
    {
        Remember(packet);
        ChatBox::Receive(packet);
    }

    void NetChat::Remember(const ChatPacket& packet)
    {
        if (_history.size() == 64)
        {
            _history.erase(_history.begin());
            _entries.erase(_entries.begin());
        }
        const std::string text = packet.Text.value_or("");
        _history.push_back(packet.Kind == ChatPacket::KindSystem ? text
            : std::string(packet.Kind == ChatPacket::KindTeam ? "[Team] " : "") + packet.Name.value_or("") + ": " + text);
        _entries.emplace_back(_history.back(), packet.Kind == ChatPacket::KindSystem);
        _revision = UncheckedAdd(_revision, 1);
    }

    void NetChat::Send(const std::string& text)
    {
        if (::MphRead::NativeRuntime::StringIsNullOrWhiteSpace(text))
        {
            return;
        }
        Network::NetSession::SendChat(text);
    }
}
