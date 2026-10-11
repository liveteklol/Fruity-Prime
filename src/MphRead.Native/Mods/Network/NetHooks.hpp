#pragma once

#include "../../Formats/Types.hpp"
#include "NetProtocol.hpp"

#include <cstdint>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    class NetHooks final
    {
    public:
        NetHooks() = delete;

        [[nodiscard]] static std::int32_t LocalSlot();
        [[nodiscard]] static bool IsPuppet(Entities::PlayerEntity& player);
        // A remote player's zoom is the state their owner reports (the
        // intent's ZoomedState, the snapshot's FlagZoomed), never toggled
        // here by their replayed press: a press is a toggle, so one replayed
        // against a state that already took it -- a press recovered late, a
        // frame with no intent -- zooms a copy its owner never zoomed.
        [[nodiscard]] static bool ZoomIsReported(Entities::PlayerEntity& player);
        [[nodiscard]] static bool KeepSlotAlive(Entities::PlayerEntity& player);
        [[nodiscard]] static bool PinPuppetsOnClients() noexcept { return _pinPuppetsOnClients; }
        static void PinPuppetsOnClients(bool value) noexcept { _pinPuppetsOnClients = value; }
        [[nodiscard]] static bool SnapshotOwnsPuppets() noexcept { return _snapshotOwnsPuppets; }
        static constexpr std::uint32_t StaleIntentFrames = 30U;
        static void SnapshotOwnsPuppets(bool value) noexcept { _snapshotOwnsPuppets = value; }
        static void AfterRemoteMovement(Entities::PlayerEntity& player);
        [[nodiscard]] static OpenTK::Mathematics::Vector3 RemoteShotOrigin(
            Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 current);
        [[nodiscard]] static OpenTK::Mathematics::Vector3 DrawnRemoteShot(
            Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 origin, OpenTK::Mathematics::Vector3 current);
        [[nodiscard]] static OpenTK::Mathematics::Vector3 RemoteShotDirection(
            Entities::PlayerEntity& player, OpenTK::Mathematics::Vector3 current);
        [[nodiscard]] static bool TryApplyRemoteInput(
            Entities::PlayerEntity& player, std::int32_t slot);
        [[nodiscard]] static bool ForceSpawn(Entities::PlayerEntity& player);
        static void AfterInput(MphRead::Scene& scene);
        static void AfterSimulation();

    private:
        static constexpr std::uint32_t SnapshotStaleFrames = 12U;

        inline static bool _pinPuppetsOnClients = false;
        inline static bool _intentPending = false;
        inline static IntentPacket _pendingIntent{};
        inline static bool _snapshotOwnsPuppets = true;

        [[nodiscard]] static bool SnapshotPositions();

        static void ApplyRemoteStates();
    };
}
