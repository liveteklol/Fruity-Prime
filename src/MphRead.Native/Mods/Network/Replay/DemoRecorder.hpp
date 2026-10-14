#pragma once

#include "Mods/Network/Replay/DemoFrameClock.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    class DemoWriter;
    struct IntentPacket;
    struct ReceivedPacket;

    class DemoRecorder final
    {
    public:
        DemoRecorder() = delete;
        DemoRecorder(const DemoRecorder&) = delete;
        DemoRecorder(DemoRecorder&&) = delete;
        DemoRecorder& operator=(const DemoRecorder&) = delete;
        DemoRecorder& operator=(DemoRecorder&&) = delete;

        [[nodiscard]] static bool IsRecording() noexcept;
        [[nodiscard]] static std::optional<std::string> CurrentPath();

        [[nodiscard]] static bool Start();
        static void Stop();

        static void Record(ReceivedPacket packet);
        // This machine's own intent, as the server would have relayed it to
        // everyone else: sections included, since a copy fires only the shot
        // events (and lays only the bombs) the intent carries.
        static void RecordOwnIntent(std::int32_t slot, const IntentPacket& intent);
        [[nodiscard]] static std::vector<std::uint8_t> OwnIntentRecord(
            std::int32_t slot, const IntentPacket& intent);
        static void RecordOwnSnapshot(std::span<const std::uint8_t> payload);

    private:
        [[nodiscard]] static std::uint32_t Frame();
        [[nodiscard]] static std::uint32_t RecordFrame(
            std::span<const std::uint8_t> packet, std::uint32_t fallbackFrame);
        [[nodiscard]] static std::string SanitizeFileName(std::string name);

        static std::unique_ptr<DemoWriter> _writer;
        static DemoFrameClock _frameClock;
        static std::optional<std::string> _currentPath;
    };
}
