#pragma once

#include "Mods/Network/Protocol/SessionProtocol.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>

namespace MphRead::Mods::Network
{
    // Keeps a bounded replay window so retransmitted lobby commands receive
    // their original result without applying their mutation a second time.
    class LobbyCommandHistory final
    {
    public:
        static constexpr std::size_t Capacity = 64;

        [[nodiscard]] std::optional<LobbyCommandResultPacket> Find(std::uint32_t commandId) const
        {
            const auto found = _results.find(commandId);
            if (found == _results.end())
            {
                return std::nullopt;
            }
            return found->second;
        }

        [[nodiscard]] bool Remember(const LobbyCommandResultPacket& result)
        {
            if (_results.contains(result.CommandId))
            {
                return false;
            }
            if (_order.size() >= Capacity)
            {
                _results.erase(_order.front());
                _order.pop_front();
            }
            _order.push_back(result.CommandId);
            _results.emplace(result.CommandId, result);
            return true;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return _results.size(); }

    private:
        std::map<std::uint32_t, LobbyCommandResultPacket> _results{};
        std::deque<std::uint32_t> _order{};
    };
}
