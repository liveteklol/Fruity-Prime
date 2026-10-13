#include "Mods/Network/Server/Core/ServerPeerLiveness.hpp"

#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    void ServerPeerLiveness::MarkSeen(double now) noexcept
    {
        _lastSeen = now;
    }

    bool ServerPeerLiveness::TimedOut(double now, double timeoutSeconds) const noexcept
    {
        return now - _lastSeen > timeoutSeconds;
    }

    bool ServerPeerLiveness::CanSendPing(double now) const noexcept
    {
        return !_pingPending || now - _pingSentAt >= PingRetrySeconds;
    }

    std::uint8_t ServerPeerLiveness::BeginPing(double now) noexcept
    {
        ++_pingId;
        _pingSentAt = now;
        _pingPending = true;
        return _pingId;
    }

    bool ServerPeerLiveness::AcceptPong(std::uint8_t pingId, double now) noexcept
    {
        if (!_pingPending || pingId != _pingId)
        {
            return false;
        }
        _pingPending = false;
        MarkSeen(now);
        std::int32_t rtt = Runtime::ConvertToInt32Net9(Runtime::RoundToEven((now - _pingSentAt) * 1000));
        rtt = std::clamp(rtt, 0, 9999);
        _pingMilliseconds = _pingMilliseconds == 0 ? rtt : (_pingMilliseconds * 2 + rtt) / 3;
        return true;
    }

    std::int32_t ServerPeerLiveness::PingMilliseconds() const noexcept
    {
        return _pingMilliseconds;
    }
}
