#pragma once

namespace MphRead::Mods::Network
{
    class SessionConnection;
    class SessionMatchIdentity;
    class SessionRuntimeState;
    class SessionSimulationCallbacks;

    // Owns the server notification and client packet for an explicit match end.
    class SessionMatchEndChannel final
    {
    public:
        SessionMatchEndChannel() = delete;

        static void Send(SessionConnection& connection, const SessionRuntimeState& runtime,
            const SessionMatchIdentity& matchIdentity, SessionSimulationCallbacks& simulation);
    };
}
