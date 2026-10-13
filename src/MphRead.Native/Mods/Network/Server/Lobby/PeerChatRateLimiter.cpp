#include "Mods/Network/Server/Lobby/PeerChatRateLimiter.hpp"

#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    PeerChatRateLimiter::Decision PeerChatRateLimiter::TryConsume(double now) noexcept
    {
        _credit = std::min(BurstCapacity, _credit + (now - _creditAt) * RefillPerSecond);
        _creditAt = now;
        if (_credit < 1.0)
        {
            const bool firstSuppressed = _suppressed == 0;
            ::MphRead::NativeRuntime::IncrementInPlace(_suppressed);
            return firstSuppressed ? Decision::FirstSuppressed : Decision::Suppressed;
        }
        _credit -= 1.0;
        _suppressed = 0;
        return Decision::Allowed;
    }

    void PeerChatRateLimiter::Reset() noexcept
    {
        _credit = BurstCapacity;
        _creditAt = 0.0;
        _suppressed = 0;
    }
}
