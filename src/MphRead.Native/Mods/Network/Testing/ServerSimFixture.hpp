#pragma once

#include "Mods/Network/Server/Match/ServerSim.hpp"

#include <stdexcept>

namespace MphRead::Mods::Network
{
    // Diagnostics can inspect the running scene without friending each check
    // or exposing ServerSim's owning shared_ptr and lifecycle state.
    class ServerSimFixture final
    {
    public:
        [[nodiscard]] static MphRead::Scene& RequireScene(ServerSim& simulation)
        {
            if (simulation._scene == nullptr)
            {
                throw std::logic_error("server simulation has no running scene");
            }
            return *simulation._scene;
        }
    };
}
