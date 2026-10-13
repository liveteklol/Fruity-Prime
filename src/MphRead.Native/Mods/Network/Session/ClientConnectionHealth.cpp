#include "Mods/Network/Session/ClientConnectionHealth.hpp"

#include "NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <utility>

namespace MphRead::Mods::Network
{
    void ClientConnectionHealth::Begin(double now) noexcept
    {
        _lastServerPacket = now;
    }

    bool ClientConnectionHealth::RecordServerPacket(double now) noexcept
    {
        if (_lastServerPacket > 0.0 && now > _lastServerPacket)
        {
            _longestServerSilence = std::max(_longestServerSilence, now - _lastServerPacket);
        }
        _lastServerPacket = now;
        const bool recovered = _connectionLost;
        _connectionLost = false;
        return recovered;
    }

    bool ClientConnectionHealth::TimedOut(double now, double timeoutSeconds) const noexcept
    {
        return now - _lastServerPacket > timeoutSeconds;
    }

    bool ClientConnectionHealth::ReannouncementDue(double now, double silenceBeforeRejoin) const noexcept
    {
        return !_reannounced && now - _lastServerPacket > silenceBeforeRejoin;
    }

    ClientConnectionHealth::Reannouncement ClientConnectionHealth::StartReannouncement(
        double now, double silenceBeforeRejoin) noexcept
    {
        if (!ReannouncementDue(now, silenceBeforeRejoin))
        {
            return {};
        }

        ::MphRead::NativeRuntime::IncrementInPlace(_reannouncements);
        _reannounced = true;
        const bool becameLost = !_connectionLost;
        _connectionLost = true;
        return { true, becameLost, now - _lastServerPacket };
    }

    bool ClientConnectionHealth::Admit() noexcept
    {
        if (!_reannounced)
        {
            return false;
        }
        _reannounced = false;
        return true;
    }

    void ClientConnectionHealth::Refuse(RefusedPacket reason) noexcept
    {
        _refusedReason = std::move(reason);
        _refused = true;
    }

    void ClientConnectionHealth::ClearRefusal() noexcept
    {
        _refused = false;
        _refusedReason = {};
    }

    void ClientConnectionHealth::Reset() noexcept
    {
        _connectionLost = false;
        _lastServerPacket = 0.0;
        _reannouncements = 0;
        _longestServerSilence = 0.0;
        _reannounced = false;
        ClearRefusal();
    }
}
