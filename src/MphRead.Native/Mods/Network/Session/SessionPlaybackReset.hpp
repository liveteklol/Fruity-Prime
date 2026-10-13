#pragma once

namespace MphRead::Mods::Network
{
    class ClientReplicaStream;
    class RemotePlayerChannel;
    class SessionRuntimeState;
    class SessionTrafficMetrics;

    // Resets session, prediction and weapon state before a recorded match rewinds.
    class SessionPlaybackReset final
    {
    public:
        SessionPlaybackReset() = delete;

        static void Apply(SessionRuntimeState& runtime, ClientReplicaStream& replicaStream,
            RemotePlayerChannel& remotePlayers, SessionTrafficMetrics& traffic);
    };
}
