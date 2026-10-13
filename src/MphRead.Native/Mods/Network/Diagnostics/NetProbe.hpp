#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    struct NetProbeResult
    {
        bool Ok;
        std::string Message;
    };

    class NetProbe final
    {
    public:
        NetProbe() = delete;
        NetProbe(const NetProbe&) = delete;
        NetProbe(NetProbe&&) = delete;
        NetProbe& operator=(const NetProbe&) = delete;
        NetProbe& operator=(NetProbe&&) = delete;

        [[nodiscard]] static NetProbeResult Probe(const std::string& address,
            std::int32_t port, std::int32_t timeoutMs = 3000);
    };
}
