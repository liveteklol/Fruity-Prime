#pragma once

#include "NativeRuntime/System/Guid.hpp"
#include <span>
#include <string>

namespace MphRead::Mods::Network
{
    class DedicatedServer;

    class ServerLaunchOptions final
    {
    public:
        static void Apply(DedicatedServer& server, std::span<const std::string> arguments);
        [[nodiscard]] static bool ParseOwnerToken(const std::string& text, NativeRuntime::Guid& token);
    };
}
