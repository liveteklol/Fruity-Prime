#pragma once

#include "Formats/Enums.hpp"
#include "Mods/Network/Domain/ShotKey.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace MphRead::Mods::Network
{
    // Bounded authority hit history used to pair a remote claim with the
    // authority's own resolved hit. Damage application and verdicts stay out.
    class AuthorityHitLedger final
    {
    public:
        static constexpr std::size_t SlotCapacity = 8;
        static constexpr std::size_t HistoryDepth = 8;
        static constexpr std::int32_t AckMatchFrames = 120;
        static constexpr std::int32_t LaunchMatchFrames = 4;

        enum class MatchKind : std::uint8_t
        {
            None,
            Launch,
            AckWindow
        };

        struct Match final
        {
            MatchKind Kind = MatchKind::None;
            std::int32_t Damage = 0;

            [[nodiscard]] explicit operator bool() const noexcept { return Kind != MatchKind::None; }
        };

        void Record(std::int32_t attacker, std::int32_t victim, std::uint32_t frame,
            std::uint32_t ack, std::uint32_t launch, std::int32_t damage, bool used,
            ::MphRead::BeamType beam, std::optional<ShotKey> launchKey = std::nullopt);
        [[nodiscard]] std::int32_t NearestOffset(std::int32_t attacker, std::int32_t victim,
            std::uint32_t arrived) const;
        [[nodiscard]] Match Take(std::int32_t attacker, std::int32_t victim, std::uint32_t claimAck,
            std::uint32_t claimLaunch, std::uint32_t arrived, std::int32_t window,
            std::optional<ShotKey> strictKey = std::nullopt,
            ::MphRead::BeamType strictBeam = ::MphRead::BeamType::None) noexcept;
        void ClearPair(std::int32_t attacker, std::int32_t victim) noexcept;
        void ClearSlot(std::int32_t slot) noexcept;
        void Reset() noexcept;

    private:
        struct Entry final
        {
            std::uint32_t Frame = 0;
            std::uint32_t Ack = 0;
            std::uint32_t Launch = 0;
            std::int32_t Damage = 0;
            ShotKey Key{};
            ::MphRead::BeamType Beam = ::MphRead::BeamType::None;
            bool Used = false;
        };

        using PairHistory = std::array<Entry, HistoryDepth>;
        std::array<std::array<PairHistory, SlotCapacity>, SlotCapacity> _entries{};
        std::array<std::array<std::size_t, SlotCapacity>, SlotCapacity> _next{};
    };
}
