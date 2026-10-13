#pragma once

#include "Mods/Network/Session/NetSession.hpp"

namespace MphRead::Mods::Network
{
    // Keeps white-box fixture setup behind one test-only boundary. Production
    // diagnostics use NetSession's public session API instead of friending each
    // diagnostic class to its mutable implementation state.
    class NetSessionFixture final
    {
    public:
        struct State final
        {
            NetRole Role = NetRole::Offline;
            std::int32_t LocalSlot = -1;
            std::uint32_t Frame = 0;
            SessionAuthorityState Authority{};
        };

        [[nodiscard]] static State Capture() noexcept
        {
            return {NetSession::_context._runtime.Role(), NetSession::_context._runtime.LocalSlot(),
                NetSession::_context._runtime.Frame(), NetSession::_context._authorityState};
        }

        static void Restore(const State& state) noexcept
        {
            NetSession::_context._runtime.SetRole(state.Role);
            NetSession::_context._authorityState = state.Authority;
            NetSession::_context._runtime.SetLocalSlot(state.LocalSlot);
            NetSession::_context._runtime.SetFrame(state.Frame);
        }

        static void SetRole(NetRole role) noexcept { NetSession::_context._runtime.SetRole(role); }
        static void SetLocalSlot(std::int32_t slot) noexcept { NetSession::_context._runtime.SetLocalSlot(slot); }
        static void SetFrame(std::uint32_t frame) noexcept { NetSession::_context._runtime.SetFrame(frame); }
        static void RevokeAuthority() noexcept { NetSession::_context._authorityState.Revoke(); }

        static void ReceiveSnapshot(ReceivedPacket packet)
        {
            NetSession::HandleSnapshot(std::move(packet));
        }
    };
}
