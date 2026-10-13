#pragma once

namespace MphRead::Mods::Network
{
    class NetDiagnostics final
    {
    public:
        NetDiagnostics() = delete;

        [[nodiscard]] static bool Enabled();
        static void SetEnabled(bool value) noexcept;
        static void Report(double time);

    private:
        inline static double _lastReport = 0.0;
        inline static bool _enabled = false;
        inline static bool _checked = false;
    };
}
