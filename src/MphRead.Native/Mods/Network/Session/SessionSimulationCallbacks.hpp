#pragma once

#include "Mods/Network/Session/SessionPorts.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace MphRead::Mods::Network
{
    // Owns the callbacks that connect the session facade to its authoritative
    // simulation. They are configured once per server session and never
    // rebuilt in the snapshot hot path.
    class SessionSimulationCallbacks final
    {
    public:
        void Configure(SnapshotSink snapshotSink, std::function<void()> matchEnded);
        void Reset() noexcept;

        [[nodiscard]] bool HasSnapshotSink() const noexcept;
        void PublishSnapshot(std::span<const std::uint8_t> payload) const;
        void NotifyMatchEnded() const;

    private:
        SnapshotSink _snapshotSink{};
        std::function<void()> _matchEnded{};
    };
}
