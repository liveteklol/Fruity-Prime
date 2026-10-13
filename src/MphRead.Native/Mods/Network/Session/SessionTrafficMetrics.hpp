#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Owns the packet and state counters exposed by the active session.
    class SessionTrafficMetrics final
    {
    public:
        [[nodiscard]] std::int64_t SnapshotsReceived() const noexcept { return _snapshotsReceived; }
        [[nodiscard]] std::int64_t SnapshotsSent() const noexcept { return _snapshotsSent; }
        [[nodiscard]] std::int64_t StatesApplied() const noexcept { return _statesApplied; }
        [[nodiscard]] std::int64_t IntentsReceived() const noexcept { return _intentsReceived; }
        [[nodiscard]] std::int64_t IntentsSent() const noexcept { return _intentsSent; }
        [[nodiscard]] std::int64_t IntentBytesSent() const noexcept { return _intentBytesSent; }

        void RecordSnapshotReceived() noexcept;
        void RecordSnapshotSent() noexcept;
        void RecordStateApplied() noexcept;
        void RecordIntentReceived() noexcept;
        void RecordIntentSent(std::int64_t bytes) noexcept;
        void Reset() noexcept;

    private:
        std::int64_t _snapshotsReceived = 0;
        std::int64_t _snapshotsSent = 0;
        std::int64_t _statesApplied = 0;
        std::int64_t _intentsReceived = 0;
        std::int64_t _intentsSent = 0;
        std::int64_t _intentBytesSent = 0;
    };
}
