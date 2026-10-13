#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns the client/server simulation-authority lease and its measurements.
    // Session transport, reconnect policy, and match identity remain outside.
    class SessionAuthorityState final
    {
    public:
        [[nodiscard]] bool IsAuthority() const noexcept { return _isAuthority; }
        [[nodiscard]] std::int32_t StandDowns() const noexcept { return _standDowns; }
        [[nodiscard]] std::int64_t Frames() const noexcept { return _frames; }

        void BecomeServerAuthority() noexcept;
        [[nodiscard]] bool BecomeClientAuthority() noexcept;
        [[nodiscard]] bool StandDown() noexcept;
        void Revoke() noexcept { _isAuthority = false; }
        [[nodiscard]] bool ConsumeStateSync() noexcept;
        void RecordServerFrame() noexcept;
        void RecordAuthorityFrame() noexcept;
        void Reset() noexcept;

    private:
        void RecordFrame() noexcept;

        bool _isAuthority = false;
        bool _needsStateSync = false;
        std::int32_t _standDowns = 0;
        std::int64_t _frames = 0;
    };
}
