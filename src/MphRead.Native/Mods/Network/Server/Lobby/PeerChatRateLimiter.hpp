#pragma once

namespace MphRead::Mods::Network
{
    // Per-peer chat flood control. It owns only its token-bucket state; packet
    // validation, team routing, transport, and logging remain with the server.
    class PeerChatRateLimiter final
    {
    public:
        enum class Decision
        {
            Allowed,
            Suppressed,
            FirstSuppressed
        };

        static constexpr double BurstCapacity = 3.0;
        static constexpr double RefillPerSecond = 0.5;

        [[nodiscard]] Decision TryConsume(double now) noexcept;
        void Reset() noexcept;

    private:
        double _credit = BurstCapacity;
        double _creditAt = 0.0;
        int _suppressed = 0;
    };
}
