#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    // C# internal static class DemoInfo.
    class DemoInfo final
    {
    public:
        DemoInfo() = delete;
        DemoInfo(const DemoInfo&) = delete;
        DemoInfo(DemoInfo&&) = delete;
        DemoInfo& operator=(const DemoInfo&) = delete;
        DemoInfo& operator=(DemoInfo&&) = delete;

        [[nodiscard]] static std::int32_t Print(const std::string& path, bool replay);

    private:
        [[nodiscard]] static std::int32_t Replay(const std::string& path);
    };
}
