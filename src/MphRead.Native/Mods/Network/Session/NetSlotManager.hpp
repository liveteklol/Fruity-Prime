#pragma once

#include "Entities/Players/PlayerEntity.hpp"

#include <array>
#include <cstdint>

namespace MphRead::Mods::Network
{
    class NetSlotManager final
    {
    private:
        static std::array<bool, Entities::PlayerEntity::SlotCapacity> _activated;

    public:
        NetSlotManager() = delete;

        static void Reset();
        static void Sync();

    private:
        static void Activate(Entities::PlayerEntity& player, std::int32_t slot);
        [[nodiscard]] static std::int32_t CountActive();

    public:
        static void ReleaseSlot(std::int32_t slot);

    private:
        static void SyncTeam(Entities::PlayerEntity& player, std::int32_t slot);
        [[nodiscard]] static bool TeamIndexTaken(std::int32_t teamIndex, std::int32_t slot);
        static void Deactivate(Entities::PlayerEntity& player, std::int32_t slot);
    };
}
