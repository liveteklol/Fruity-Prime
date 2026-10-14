#include "Mods/Network/Session/SessionIntentPublisher.hpp"

#include "Mods/Network/Replay/DemoRecorder.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Session/SessionClaimChannel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    namespace
    {
        [[nodiscard]] std::span<const std::uint8_t> First(
            const std::array<std::uint8_t, NetConfig::MaxPacketSize>& buffer, std::size_t count)
        {
            return std::span<const std::uint8_t>(buffer).first(count);
        }
    }

    void SessionIntentPublisher::Send(IntentPacket intent, bool gameplayFrozen,
        SessionConnection& connection, const SessionRuntimeState& runtime,
        const SessionMatchIdentity& matchIdentity, SessionTrafficMetrics& traffic)
    {
        if (gameplayFrozen)
        {
            return;
        }
        if (!connection.IsOpen() || runtime.Role() != NetRole::Client || !connection.HasHostEndpoint())
        {
            return;
        }

        // A deferred intent keeps the frame it was captured on (NetHooks sends
        // it after the simulation, to carry the ray the frame really fired).
        if (intent.Frame == 0)
        {
            intent.Frame = runtime.Frame();
        }
        intent.MatchId = matchIdentity.MatchId(false);
        intent.AuthorityEpoch = matchIdentity.AuthorityEpoch(false);
        intent.SlotGeneration = NetPlayerLifecycle::Generation(runtime.LocalSlot());
        intent.LifeId = NetPlayerLifecycle::Get(runtime.LocalSlot());

        const std::size_t size = intent.WireSize();
        intent.Write(std::span<std::uint8_t>(connection.Buffer().data(), size));
        connection.SendToHost(PacketType::Intent, First(connection.Buffer(), size));
        traffic.RecordIntentSent(static_cast<std::int64_t>(size));

        if (runtime.LocalSlot() >= 0)
        {
            DemoRecorder::RecordOwnIntent(runtime.LocalSlot(), intent);
        }
        SessionClaimChannel::SendClientClaims(connection);
    }
}
