#pragma once

#include "Mods/EndScreen.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    struct VoteStatePacket;

    class MapVote final
    {
    public:
        MapVote() = delete;
        MapVote(const MapVote&) = delete;
        MapVote(MapVote&&) = delete;
        MapVote& operator=(const MapVote&) = delete;
        MapVote& operator=(MapVote&&) = delete;

        [[nodiscard]] static bool Active() noexcept { return _active; }
        [[nodiscard]] static std::optional<std::string> RoomKey()
        {
            return _roomKey;
        }
        [[nodiscard]] static std::optional<std::string> Proposer()
        {
            return _proposer;
        }
        [[nodiscard]] static std::int32_t Yes() noexcept { return _yes; }
        [[nodiscard]] static std::int32_t No() noexcept { return _no; }
        [[nodiscard]] static std::int32_t Eligible() noexcept { return _eligible; }
        [[nodiscard]] static std::int32_t Needed() noexcept { return _needed; }
        [[nodiscard]] static std::int32_t Seconds() noexcept { return _seconds; }
        [[nodiscard]] static bool Answered() noexcept { return _answered; }
        [[nodiscard]] static bool Supported() noexcept { return _supported; }
        [[nodiscard]] static bool Disabled() noexcept { return _disabled; }

        [[nodiscard]] static bool CanPropose() noexcept
        {
            return _supported && !_disabled && !_active && _seconds == 0;
        }

        [[nodiscard]] static std::string WhyNotProposing();

        static void NoteLayout(
            MphRead::Mods::EndScreen::Hit accept,
            MphRead::Mods::EndScreen::Hit deny) noexcept;

        [[nodiscard]] static bool HandleClick();
        [[nodiscard]] static std::shared_ptr<std::vector<float>> TouchTargets();

        static void Reset();
        static void Apply(VoteStatePacket state);
        static void Propose(const std::optional<std::string>& roomKey);
        static void Cast(bool yes);

        [[nodiscard]] static std::string PromptLine();
        [[nodiscard]] static std::string TallyLine();

    private:
        static bool _active;
        static std::optional<std::string> _roomKey;
        static std::optional<std::string> _proposer;
        static std::int32_t _yes;
        static std::int32_t _no;
        static std::int32_t _eligible;
        static std::int32_t _needed;
        static std::int32_t _seconds;
        static bool _answered;
        static bool _supported;
        static bool _disabled;

        static MphRead::Mods::EndScreen::Hit _hitAccept;
        static MphRead::Mods::EndScreen::Hit _hitDeny;
    };
}
