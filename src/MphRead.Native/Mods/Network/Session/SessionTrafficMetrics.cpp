#include "Mods/Network/Session/SessionTrafficMetrics.hpp"

#include "NativeRuntime/System/Managed.hpp"

namespace MphRead::Mods::Network
{
    void SessionTrafficMetrics::RecordSnapshotReceived() noexcept
    {
        ::MphRead::NativeRuntime::IncrementInPlace(_snapshotsReceived);
    }

    void SessionTrafficMetrics::RecordSnapshotSent() noexcept
    {
        ::MphRead::NativeRuntime::IncrementInPlace(_snapshotsSent);
    }

    void SessionTrafficMetrics::RecordStateApplied() noexcept
    {
        ::MphRead::NativeRuntime::IncrementInPlace(_statesApplied);
    }

    void SessionTrafficMetrics::RecordIntentReceived() noexcept
    {
        ::MphRead::NativeRuntime::IncrementInPlace(_intentsReceived);
    }

    void SessionTrafficMetrics::RecordIntentSent(std::int64_t bytes) noexcept
    {
        ::MphRead::NativeRuntime::IncrementInPlace(_intentsSent);
        _intentBytesSent = ::MphRead::NativeRuntime::UncheckedAdd(_intentBytesSent, bytes);
    }

    void SessionTrafficMetrics::Reset() noexcept
    {
        _snapshotsReceived = 0;
        _snapshotsSent = 0;
        _statesApplied = 0;
        _intentsReceived = 0;
        _intentsSent = 0;
        _intentBytesSent = 0;
    }
}
