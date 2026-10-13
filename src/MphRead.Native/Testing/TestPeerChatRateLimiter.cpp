#include "Mods/Network/Server/Lobby/PeerChatRateLimiter.hpp"

#include <iostream>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using MphRead::Mods::Network::PeerChatRateLimiter;

    bool passed = true;
    PeerChatRateLimiter limiter;
    passed &= Check(limiter.TryConsume(100.0) == PeerChatRateLimiter::Decision::Allowed
        && limiter.TryConsume(100.0) == PeerChatRateLimiter::Decision::Allowed
        && limiter.TryConsume(100.0) == PeerChatRateLimiter::Decision::Allowed,
        "initial burst admits three messages");
    passed &= Check(limiter.TryConsume(100.0) == PeerChatRateLimiter::Decision::FirstSuppressed
        && limiter.TryConsume(100.1) == PeerChatRateLimiter::Decision::Suppressed,
        "first and repeated over-limit messages are distinguished");
    passed &= Check(limiter.TryConsume(102.0) == PeerChatRateLimiter::Decision::Allowed,
        "two seconds refill exactly one message and clear the suppression run");
    passed &= Check(limiter.TryConsume(102.0) == PeerChatRateLimiter::Decision::FirstSuppressed,
        "a later over-limit run gets one fresh log event");

    limiter.Reset();
    passed &= Check(limiter.TryConsume(200.0) == PeerChatRateLimiter::Decision::Allowed
        && limiter.TryConsume(200.0) == PeerChatRateLimiter::Decision::Allowed
        && limiter.TryConsume(200.0) == PeerChatRateLimiter::Decision::Allowed
        && limiter.TryConsume(200.0) == PeerChatRateLimiter::Decision::FirstSuppressed,
        "reset restores the initial burst and suppression state");
    return passed ? 0 : 1;
}
