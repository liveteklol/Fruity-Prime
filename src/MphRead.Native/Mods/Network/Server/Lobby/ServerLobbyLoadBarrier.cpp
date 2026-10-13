#include "Mods/Network/Server/Lobby/ServerLobbyLoadBarrier.hpp"

namespace MphRead::Mods::Network
{
    bool ServerLobbyLoadBarrier::AcceptLoaded(
        ServerLobbyState& state,
        std::int32_t slot,
        std::uint16_t packetMatchId,
        std::uint16_t currentMatchId) noexcept
    {
        if (packetMatchId != currentMatchId
            || state.Phase() != SessionPhase::Starting
            || !state.IsExpectedLoadedSlot(slot)
            || state.IsLoadedSlot(slot))
        {
            return false;
        }

        state.MarkLoadedSlot(slot);
        return true;
    }

    ServerLobbyLoadDecision ServerLobbyLoadBarrier::Evaluate(
        const ServerLobbyState& state, double now) noexcept
    {
        if (state.Phase() != SessionPhase::Starting)
        {
            return {};
        }
        if (!state.LoadBarrierComplete() && now < state.StartDeadline())
        {
            return { ServerLobbyLoadDecision::Action::Wait, false };
        }
        return { ServerLobbyLoadDecision::Action::StartMatch, now >= state.StartDeadline() };
    }
}
