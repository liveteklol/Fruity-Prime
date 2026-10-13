#pragma once

#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <cstdint>
#include <optional>

namespace MphRead::Mods::Network
{
    // Owns the accepted match identity and its monotonic transition policy.
    // SessionMatchTransition coordinates network and game-state effects.
    class SessionMatchIdentity final
    {
    public:
        enum class Rejection : std::uint8_t
        {
            None,
            InvalidIdentity,
            StaleAuthority,
            StaleMatch,
            EndingRegression
        };

        struct Transition final
        {
            Rejection Rejected = Rejection::None;
            bool HadPrevious = false;
            bool NewMatch = false;
            bool NewAuthority = false;
            bool RoomChanged = false;

            [[nodiscard]] bool Accepted() const noexcept { return Rejected == Rejection::None; }
        };

        [[nodiscard]] const std::optional<MatchStatePacket>& State() const noexcept { return _state; }
        [[nodiscard]] bool HasMatch() const noexcept { return _state.has_value(); }
        [[nodiscard]] std::uint16_t MatchId(bool host) const noexcept;
        [[nodiscard]] std::uint64_t AuthorityEpoch(bool host) const noexcept;

        [[nodiscard]] Transition Apply(const MatchStatePacket& state);
        void SetAuthoritative(const MatchStatePacket& state) { _state = state; }
        void Reset() noexcept { _state.reset(); }

    private:
        std::optional<MatchStatePacket> _state;
    };
}
