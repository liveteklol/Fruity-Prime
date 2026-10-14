#pragma once

#include "Entities/Players/PlayerEntity.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    class NetUnlaggedFixture;

    class NetUnlagged final
    {
    public:
        NetUnlagged() = delete;

        static constexpr std::int32_t HistoryFrames = 128;
        static constexpr std::int32_t DefaultMaxRewindFrames = 45;
        static constexpr std::int32_t LegacyMaxRewindFrames = 24;
        static constexpr std::int32_t MaxRewindCeiling = HistoryFrames - 8;

        [[nodiscard]] static std::int32_t MaxRewindFrames() noexcept { return _maxRewindFrames; }
        static void MaxRewindFrames(std::int32_t value) noexcept { _maxRewindFrames = value; }
        [[nodiscard]] static bool ConfigureMaxRewind(const std::optional<std::string>& value);

        [[nodiscard]] static bool Enabled() noexcept { return _enabled; }
        static void SetEnabled(bool value) noexcept { _enabled = value; }
        [[nodiscard]] static bool PressAgeEnabled() noexcept { return _pressAgeEnabled; }
        static void PressAgeEnabled(bool value) noexcept { _pressAgeEnabled = value; }

        [[nodiscard]] static std::int64_t StalePresses() noexcept { return _stalePresses; }
        [[nodiscard]] static std::int64_t StalePressFrames() noexcept { return _stalePressFrames; }
        [[nodiscard]] static std::int64_t ClampErrorSamples() noexcept { return _clampErrorSamples; }
        [[nodiscard]] static double ClampErrorSum() noexcept { return _clampErrorSum; }
        [[nodiscard]] static double ClampErrorVerticalSum() noexcept { return _clampErrorVerticalSum; }
        [[nodiscard]] static float ClampErrorWorst() noexcept { return _clampErrorWorst; }
        [[nodiscard]] static float ClampErrorWorstVertical() noexcept { return _clampErrorWorstVertical; }
        [[nodiscard]] static std::int64_t ShotsCompensated() noexcept { return _shotsCompensated; }
        [[nodiscard]] static bool AnyBotInPlay();
        [[nodiscard]] static std::int64_t FramesRewound() noexcept { return _framesRewound; }
        [[nodiscard]] static std::int32_t WorstRewind() noexcept { return _worstRewind; }
        [[nodiscard]] static std::int64_t ShotsClamped() noexcept { return _shotsClamped; }
        [[nodiscard]] static std::int64_t FramesRefused() noexcept { return _framesRefused; }
        [[nodiscard]] static std::int32_t WorstRequested() noexcept { return _worstRequested; }
        [[nodiscard]] static std::int64_t CatchUpSteps() noexcept { return _catchUpSteps; }
        [[nodiscard]] static std::int64_t CatchUpHits() noexcept { return _catchUpHits; }
        [[nodiscard]] static std::int64_t HistoryMisses() noexcept { return _historyMisses; }

        static void ResetSlot(std::int32_t slot);
        static void Reset();
        static void Record(std::uint32_t frame);
        [[nodiscard]] static std::optional<std::int32_t> PrimeHunterAt(std::uint32_t frame) noexcept;
        [[nodiscard]] static bool PositionAt(std::int32_t slot, std::uint32_t frame,
            std::uint16_t expectedGeneration, std::uint16_t expectedLife, OpenTK::Mathematics::Vector3& position);
        // Whether this copy of the player, in that life, stood within `radius`
        // of `point` in some frame from `from` to `to` that the history holds.
        [[nodiscard]] static bool PassedNear(std::int32_t slot, std::uint16_t expectedGeneration,
            std::uint16_t expectedLife, OpenTK::Mathematics::Vector3 point, float radius,
            std::uint32_t from, std::uint32_t to);
        static void BeginShot(Entities::PlayerEntity& shooter);
        [[nodiscard]] static std::uint32_t LaunchFrameFor(Entities::PlayerEntity& shooter);
        static void Restore();
        static void EndShot(Entities::PlayerEntity& shooter);
        [[nodiscard]] static std::string Describe();
        [[nodiscard]] static std::string DescribeDepths();

    private:
        friend class NetUnlaggedFixture;

        inline static std::array<std::int64_t, Entities::PlayerEntity::SlotCapacity> ClampedByShooter{};
        inline static std::int64_t _rewindsSkipped = 0;
        inline static std::array<std::int64_t, HistoryFrames + 1> DepthHistogram{};

        static constexpr std::int32_t Slots = Entities::PlayerEntity::SlotCapacity;

        [[nodiscard]] static double RewindFor(std::int32_t slot, std::int32_t& requested);
        [[nodiscard]] static bool Simulating();
        static void MeasureClampError(std::int32_t shooterSlot, std::int32_t requested, std::int32_t served);
        [[nodiscard]] static bool Reconcile(std::int32_t exceptSlot, double targetFrame);

        inline static std::int32_t _maxRewindFrames = DefaultMaxRewindFrames;
        inline static bool _enabled = true;
        inline static bool _pressAgeEnabled = false;
        inline static std::int64_t _stalePresses = 0;
        inline static std::int64_t _stalePressFrames = 0;
        inline static std::int64_t _clampErrorSamples = 0;
        inline static double _clampErrorSum = 0;
        inline static double _clampErrorVerticalSum = 0;
        inline static float _clampErrorWorst = 0;
        inline static float _clampErrorWorstVertical = 0;
        inline static std::int64_t _shotsCompensated = 0;
        inline static std::int64_t _framesRewound = 0;
        inline static std::int32_t _worstRewind = 0;
        inline static std::int64_t _shotsClamped = 0;
        inline static std::int64_t _framesRefused = 0;
        inline static std::int32_t _worstRequested = 0;
        inline static std::int64_t _catchUpSteps = 0;
        inline static std::int64_t _catchUpHits = 0;
        inline static std::int64_t _historyMisses = 0;

        inline static std::array<std::array<std::uint16_t, HistoryFrames>, Slots> _life{};
        inline static std::array<std::array<std::uint16_t, HistoryFrames>, Slots> _generation{};
        inline static std::array<std::array<OpenTK::Mathematics::Vector3, HistoryFrames>, Slots> _position{};
        inline static std::array<std::array<bool, HistoryFrames>, Slots> _altForm{};
        inline static std::array<std::array<bool, HistoryFrames>, Slots> _inPlay{};
        inline static std::array<std::uint32_t, HistoryFrames> _stamp{};
        inline static std::array<std::uint32_t, HistoryFrames> _primeHunterStamp{};
        inline static std::array<std::int32_t, HistoryFrames> _primeHunter{};
        inline static std::uint32_t _newest = 0;

        inline static std::array<OpenTK::Mathematics::Vector3, Slots> _restore{};
        inline static std::array<bool, Slots> _moved{};
        inline static bool _reconciled = false;

        inline static std::vector<bool> _beamsBefore = std::vector<bool>(16);
        inline static Entities::PlayerEntity* _shooter = nullptr;
        inline static std::int32_t _rewind = 0;
        inline static bool _inProgress = false;
    };
}
