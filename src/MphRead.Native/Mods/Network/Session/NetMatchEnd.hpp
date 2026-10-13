#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    class NetMatchEnd final
    {
    public:
        NetMatchEnd() = delete;

        static void Reset();
        [[nodiscard]] static bool MayEndOnScore();
        [[nodiscard]] static bool InIntermission();
        static void Sync();
        [[nodiscard]] static bool ShouldLeaveAfterMatch();

    private:
        static constexpr std::uint32_t ReportInterval = 15U;
        static constexpr std::uint32_t StrandedFrames = 60U * 12U;

        static std::uint32_t _lastReport;
        static bool _reported;
        static bool _acknowledged;
        static std::uint32_t _strandedSince;

        static void RecoverIfStranded(bool serverEnding);
    };
}
