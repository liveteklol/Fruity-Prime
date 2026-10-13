#pragma once

#include "Mods/Network/Replay/DemoFile.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace MphRead::Mods::Network
{
    class DemoPlayback final
    {
    public:
        DemoPlayback() = delete;
        DemoPlayback(const DemoPlayback&) = delete;
        DemoPlayback(DemoPlayback&&) = delete;
        DemoPlayback& operator=(const DemoPlayback&) = delete;
        DemoPlayback& operator=(DemoPlayback&&) = delete;

        [[nodiscard]] static bool IsActive() noexcept;
        [[nodiscard]] static bool AtEnd() noexcept;
        [[nodiscard]] static std::optional<std::string> LastError();

        [[nodiscard]] static bool Join(const std::string& path, std::int32_t timeoutMs = 8000);
        static void PumpFrame();
        static void Stop();

    private:
        static constexpr std::uint32_t JoinSearchFrames = 60U * 20U;
        static constexpr std::uint32_t JoinGraceFrames = 120U;

        [[nodiscard]] static bool Rewind(const std::string& path);

        static std::unique_ptr<DemoReader> _reader;
        static std::optional<DemoRecord> _pending;
        static std::uint32_t _frame;
        static bool _started;
        static bool _isActive;
        static std::optional<std::string> _lastError;
    };
}
