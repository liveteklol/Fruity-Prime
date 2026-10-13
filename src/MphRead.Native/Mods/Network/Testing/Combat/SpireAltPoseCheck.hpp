#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    // Spire's alt attack on a headless authority: the rock collision pose
    // has to keep moving although no draw pass ever runs.
    class SpireAltPoseCheck final
    {
    public:
        SpireAltPoseCheck() = delete;

        [[nodiscard]] static std::int32_t Run(const std::string& room);
    };
}
