#include "Mods/Network/Session/SessionRuntimePump.hpp"

#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Chat/ChatBox.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <cstdint>
#include <utility>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    void SessionRuntimePump::Update(Ports& ports, double time)
    {
        if (ports.runtime.Role() == NetRole::Client && !ports.playbackActive)
        {
            time = ports.clock();
        }
        if (ports.runtime.Role() == NetRole::Server)
        {
            ports.runtime.AdvanceFrame();
            ports.authority.RecordServerFrame();
            return;
        }
        if (!ports.connection.IsOpen())
        {
            return;
        }

        ports.runtime.AdvanceFrame();
        ports.authority.RecordAuthorityFrame();

        for (ReceivedPacket packet : ports.connection.Drain())
        {
            ports.recordPacket(packet);
            ports.handlePacket(std::move(packet), time);
        }
        ports.pumpLobby(time);
        if (ports.runtime.Role() == NetRole::Host)
        {
            ports.dropTimedOutPeers(time);
            if (ports.runtime.Frame() % 60U == 0U)
            {
                ports.broadcastHostControl();
            }
        }
        else if (ports.runtime.Role() == NetRole::Client
            && (ports.runtime.LocalSlot() < 0 || ports.connectionHealth.Reannounced())
            && ports.runtime.Frame() % 60U == 0U)
        {
            ports.sendHello();
        }
        else if (ports.runtime.Role() == NetRole::Client
            && ports.runtime.Frame() % 60U == 0U
            && ports.connectionHealth.ReannouncementDue(time, ports.silenceBeforeRejoin))
        {
            const ClientConnectionHealth::Reannouncement reannouncement =
                ports.connectionHealth.StartReannouncement(time, ports.silenceBeforeRejoin);
            if (reannouncement.BecameLost)
            {
                Mods::Chat::ChatBox::System("Connection lost, retrying...");
            }
            Runtime::ConsoleWriteLine("[net] no word from the server; re-announcing (#"
                + Runtime::ToString(ports.connectionHealth.Reannouncements()) + ", silent for "
                + Runtime::ToString(reannouncement.SilentSeconds, "0.0") + " s)");
            NetLog::Event("server silent, re-announcing");
            ports.sendHello();
            ports.sendIdentify();
        }
        else if (ports.runtime.Role() == NetRole::Client
            && ports.runtime.Frame() % 120U == 0U
            && ports.identityNeedsRefresh())
        {
            ports.sendIdentify();
        }
    }

    void SessionRuntimePump::PumpMapTransfer(MapTransferPorts& ports)
    {
        if (!ports.connection.IsOpen())
        {
            return;
        }
        for (ReceivedPacket packet : ports.connection.Drain())
        {
            ports.handlePacket(std::move(packet), ports.clock());
        }
    }
}
