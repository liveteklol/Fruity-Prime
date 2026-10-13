#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace System::Net
{
    class IPEndPoint;
}

namespace MphRead::Mods::Network
{
    class DedicatedServer;
    class MasterReporter;

    // The games a server starts for players who cannot open a port of their
    // own: each one a DedicatedServer on a thread, on a port from a range.
    class HostPool final
    {
    public:
        std::function<void(const std::string&)> Log = [](const std::string&) {};
        std::function<std::shared_ptr<MasterReporter>()> ReporterFactory{};
        std::function<void(std::int32_t)> OnStopped{};

        void SetPorts(std::int32_t first, std::int32_t last) noexcept;
        [[nodiscard]] bool CanHost() const noexcept { return _last >= _first && _first > 0; }
        [[nodiscard]] std::int32_t Count() const noexcept { return static_cast<std::int32_t>(_hosted.size()); }
        [[nodiscard]] std::string Describe() const;

        [[nodiscard]] HostReplyPacket Start(const HostRequestPacket& request,
            const std::shared_ptr<System::Net::IPEndPoint>& asker, double now);
        void Reap(double now);
        void StopAll(const std::string& why);

    private:
        struct Hosted final
        {
            std::shared_ptr<DedicatedServer> Server{};
            std::shared_ptr<std::stop_source> Cancel{};
            std::int32_t Port = 0;
            std::string Name{};
            std::array<std::uint8_t, 4> Asker{255, 255, 255, 255};
            double StartedAt = 0;
            double LastOccupied = 0;
        };

        static constexpr double PortCooldownSeconds = 5;
        static constexpr double StartupSeconds = 180;
        static constexpr double EmptySeconds = 45;

        [[nodiscard]] std::int32_t FreePort(double now);
        void Stop(const std::shared_ptr<Hosted>& entry, const std::string& why, double now = 0);

        std::vector<std::shared_ptr<Hosted>> _hosted{};
        std::map<std::int32_t, double> _cooling{};
        std::int32_t _first = 0;
        std::int32_t _last = -1;
    };
}
