#pragma once

#include "Mods/Network/Session/NetRole.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace MphRead::Mods::Network
{
    // Owns the mutable state that defines one NetSession run and its frame clock.
    class SessionRuntimeState final
    {
    public:
        [[nodiscard]] NetRole Role() const noexcept { return _role; }
        [[nodiscard]] std::int32_t LocalSlot() const noexcept { return _localSlot; }
        [[nodiscard]] std::uint32_t Frame() const noexcept { return _frame; }
        [[nodiscard]] bool IsPlayback() const noexcept { return _playback; }
        [[nodiscard]] const std::optional<std::string>& LastError() const noexcept
        {
            return _lastError;
        }

        void SetRole(NetRole role) noexcept { _role = role; }
        void SetLocalSlot(std::int32_t slot) noexcept { _localSlot = slot; }
        void SetFrame(std::uint32_t frame) noexcept { _frame = frame; }
        void ResetFrame() noexcept { _frame = 0; }
        void AdvanceFrame() noexcept { ++_frame; }
        void SetPlayback(bool playback) noexcept { _playback = playback; }
        void ClearLastError() noexcept { _lastError.reset(); }
        void SetLastError(std::string error) { _lastError = std::move(error); }

    private:
        NetRole _role = NetRole::Offline;
        std::int32_t _localSlot = 0;
        std::uint32_t _frame = 0;
        bool _playback = false;
        std::optional<std::string> _lastError{};
    };
}
