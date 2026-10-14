#pragma once

#include "SpectatorTargetPolicy.hpp"
#include "SpectatorCameraPolicy.hpp"

#include <cstdint>
#include <optional>

namespace MphRead::Mods
{
    struct SpectatorCameraRequest final
    {
        bool FreeCamera = false;
        bool PreserveLastPose = false;
        std::optional<SpectatorTargetIdentity> Target{};
    };

    class SpectatorMode final
    {
    public:
        SpectatorMode() = delete;

        [[nodiscard]] static bool IsSpectating() noexcept;
        [[nodiscard]] static bool ShouldHideLocalPlayerModel(std::int32_t slot) noexcept;
        [[nodiscard]] static bool FreeCamera() noexcept;
        [[nodiscard]] static bool CanSpectate();

        static void Start(bool watchSomeone = false);
        // Validate the tracked identity once per simulation step and recover
        // from a death, disconnect, or reused network slot.
        static void Update();
        static void NotifyTargetChanged(std::int32_t slot) noexcept;
        static void CycleNext();

        // Move to the previous active player when driven by a controller.
        static void CyclePrevious();
        static void ToggleView();

        [[nodiscard]] static bool ShowScoreboard() noexcept;
        static void NoteScoreboard(bool down) noexcept;
        static void NoteFreeCamera(bool on) noexcept;
        [[nodiscard]] static std::optional<SpectatorCameraRequest> TakeCameraRequest() noexcept;
        [[nodiscard]] static bool CanUsePlayerCamera() noexcept;
        [[nodiscard]] static bool CanBorrowLocalCameraPose() noexcept;

        static void Rejoin();
        static void Reset() noexcept;

    private:
        [[nodiscard]] static bool Switch(std::int32_t slot, bool requestPlayerCamera);
        [[nodiscard]] static std::int32_t FindPreviousActiveSlot(std::int32_t fromSlot);
        [[nodiscard]] static std::int32_t FindNextActiveSlot(std::int32_t fromSlot);
        static void RequestCamera(bool freeCamera, bool preserveLastPose,
            std::optional<SpectatorTargetIdentity> target = std::nullopt) noexcept;

        static bool _isSpectating;
        static bool _freeCamera;
        static std::optional<SpectatorTargetIdentity> _target;
        static bool _targetChanged;
        static std::optional<SpectatorCameraRequest> _cameraRequest;
        static bool _showScoreboard;
    };
}
