#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Gameplay/Bombs/RemoteBombState.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace MphRead::Entities
{
    class BombEntity;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // A player's bombs, as their own machine holds them. The single place
    // the game asks whether a copy's bombs are its own or its owner's.
    //
    // Owner side: each bomb this machine's player places gets a sequence
    // (Laid), and that slot's intent carries bombs still standing (AttachFor).
    // Copy side: a copy lays no bomb from a replayed press; it lays the
    // reported ones it has not, at the reported place, never twice, and
    // detonates its own the owner no longer reports (Reconcile). On a
    // machine that is not the authority a copy's bomb never goes off by
    // touch (DetonatesOnContact): whether it hit somebody is its owner's
    // machine's call, and a bomb gone here and standing there is the bomb
    // nobody could see that still hurt. The authority keeps contact: it
    // decides what hits a bot.
    class NetBombs final
    {
    public:
        static constexpr std::int32_t Slots = 8; // PlayerEntity::SlotCapacity, checked in the .cpp

        static void Laid(Entities::PlayerEntity& owner, const std::shared_ptr<Entities::BombEntity>& bomb);
        // Adds this slot's authoritative standing bomb state to its intent.
        static void AttachFor(std::int32_t slot, IntentPacket& intent, std::uint32_t frame);
        static void Receive(std::int32_t slot, const IntentPacket& intent) noexcept;
        [[nodiscard]] static bool Drives(const Entities::PlayerEntity& player) noexcept;
        [[nodiscard]] static bool IsRemoteReplica(const Entities::BombEntity& bomb) noexcept;
        // Latest authoritative place reported for a standing remote bomb.
        [[nodiscard]] static std::optional<OpenTK::Mathematics::Vector3> ReportedPosition(
            std::int32_t slot, std::uint32_t sequence) noexcept;
        // Every simulation step, for every player; does nothing to one it
        // does not drive.
        static void Reconcile(Entities::PlayerEntity& player);
        [[nodiscard]] static bool DetonatesOnContact(const Entities::BombEntity& bomb) noexcept;
        // A new occupant, or a room change.
        static void Forget(std::int32_t slot) noexcept;

    private:
        struct Held final
        {
            std::weak_ptr<Entities::BombEntity> Bomb{};
            std::uint32_t Sequence = 0;
            // The owner's: where it last stood, and the frame it was first
            // found gone (0 while standing).
            OpenTK::Mathematics::Vector3 Position{};
            std::uint32_t GoneAt = 0;
        };
        using HeldBombs = std::array<Held, 8>;
        // How long a bomb gone on the owner's machine is still reported.
        static constexpr std::uint32_t GoneFrames = 15;
        [[nodiscard]] static std::shared_ptr<Entities::BombEntity> Standing(const Held& held) noexcept;
        static void Keep(HeldBombs& held, const std::shared_ptr<Entities::BombEntity>& bomb, std::uint32_t sequence);

        inline static std::uint32_t _sequence = 0;
        static std::array<HeldBombs, Slots> _owned;
        inline static std::array<RemoteBombState, Slots> _remote{};
        static std::array<HeldBombs, Slots> _copies;
        inline static std::array<std::uint32_t, Slots> _lastAltActionCue{};
    };
}
