#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace MphRead::Entities
{
    class BeamProjectileEntity;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    struct IntentPacket;
    struct PlayerState;

    // Owns slot/life identity and the resets required at its boundaries.
    class NetPlayerLifecycle final
    {
    public:
        NetPlayerLifecycle() = delete;

        [[nodiscard]] static bool ApplyingSpawn() noexcept { return _applyingSpawn; }
        static void ApplyingSpawn(bool value) noexcept { _applyingSpawn = value; }
        [[nodiscard]] static bool CanSpawn();

        [[nodiscard]] static std::uint16_t Get(std::int32_t slot) noexcept;
        [[nodiscard]] static std::uint16_t Generation(std::int32_t slot) noexcept;
        [[nodiscard]] static bool Matches(std::int32_t slot, std::uint16_t generation, std::uint16_t life) noexcept;

        static void StampProjectile(::MphRead::Entities::BeamProjectileEntity& beam,
            ::MphRead::Entities::BeamProjectileEntity* parent = nullptr);
        [[nodiscard]] static bool CurrentProjectile(const ::MphRead::Entities::BeamProjectileEntity& beam);

        static void SetOccupant(std::int32_t slot, std::uint16_t generation);
        static void OnSlotChanged(std::int32_t slot);
        static void OnSpawn(::MphRead::Entities::PlayerEntity& player);

        [[nodiscard]] static NetworkPlayerState StateOf(const PlayerState& state) noexcept;
        [[nodiscard]] static bool AcceptState(const PlayerState& state, std::uint32_t frame);
        [[nodiscard]] static bool AcceptIntent(std::int32_t slot, const IntentPacket& intent);

        static void ResetLives();
        static void Reset();
        [[nodiscard]] static std::string Describe();

        static void RecordWrongGeneration() noexcept;
        static void RecordCrossMatch() noexcept;
        static void RecordCrossAuthority() noexcept;
        static void RecordOldLifeClaim() noexcept;
        static void RecordOldLifeDamage() noexcept;

    private:
        static std::int64_t StaleLifeStates;
        static std::int64_t WrongGeneration;
        static std::int64_t InvalidResurrections;
        static std::int64_t Transitions;
        static std::int64_t Spawns;
        static std::int64_t Deaths;
        static std::int64_t OldLifeIntents;
        static std::int64_t OldLifeClaims;
        static std::int64_t OldLifeDamage;
        static std::int64_t CrossMatch;
        static std::int64_t CrossAuthority;

        static void Log(std::int32_t slot, const std::string& action, std::uint32_t frame,
            std::int32_t health, bool spawned);

        static std::array<NetLifecycleTracker, NetworkSlotCapacity> _slots;
        static bool _applyingSpawn;
    };
}
