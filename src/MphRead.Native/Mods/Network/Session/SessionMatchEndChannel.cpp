#include "Mods/Network/Session/SessionMatchEndChannel.hpp"

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetRole.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Session/SessionSimulationCallbacks.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"

#include <span>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    void SessionMatchEndChannel::Send(SessionConnection& connection,
        const SessionRuntimeState& runtime, const SessionMatchIdentity& matchIdentity,
        SessionSimulationCallbacks& simulation)
    {
        if (runtime.Role() == NetRole::Server)
        {
            simulation.NotifyMatchEnded();
            return;
        }
        if (!connection.IsOpen() || runtime.Role() != NetRole::Client || !connection.HasHostEndpoint())
        {
            return;
        }
        const bool host = runtime.Role() == NetRole::Host;
        Runtime::WriteUInt16LittleEndian(std::span<std::uint8_t>(connection.Buffer()),
            matchIdentity.MatchId(host));
        Runtime::WriteUInt64LittleEndian(std::span<std::uint8_t>(connection.Buffer()).subspan(2),
            matchIdentity.AuthorityEpoch(host));
        connection.SendToHost(PacketType::MatchEnd,
            std::span<const std::uint8_t>(connection.Buffer()).first(10));
    }
}
