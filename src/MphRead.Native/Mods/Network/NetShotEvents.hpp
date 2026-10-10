#pragma once

#include "LocalShotLog.hpp"
#include "NetProtocol.hpp"
#include "RemoteShotQueue.hpp"
#include "ShotEventLedger.hpp"
#include "../../Formats/Enums.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // A remote player's shots, as their own machine fired them (shot events,
    // IntentPacket::ShotEvent). The single place the game asks whether a
    // remote copy may fire, and with what -- weapon, charge, ray and the
    // world it was aimed in.
    //
    // A copy fires each event as it arrives, without waiting for a trigger or
    // a cooldown the owner's machine already kept, and fires nothing without
    // one. So a copy can neither invent a shot (a trigger held through a
    // respawn) nor change its weapon (the Omega Cannon unequips itself as it
    // fires, and the switch can arrive first), a shot recovered from the
    // history leaves on the ray it was fired on, and a shot fired just before
    // its owner was killed still leaves the body. The shot it fires is a
    // picture: hits are the shooter's claims (NetHitClaims), checked against
    // the event the claim names (Find), never resolved again from it.
    // Continuous fire makes no events and stays the trigger's; so do bots,
    // which send no intents.
    //
    // Owner side: Fired records the shot, SequenceOfShotFired stamps its
    // projectiles, Attach puts the history in the intent. WeaponSelect keeps syncing
    // the weapon held.
    class NetShotEvents final
    {
    public:
        static constexpr std::int32_t Slots = 8; // PlayerEntity::SlotCapacity, checked in the .cpp

        struct FiredShot final
        {
            ::MphRead::BeamType Weapon = ::MphRead::BeamType::None;
            // A Shock Coil frame: no event, and none spent.
            bool Continuous = false;
            OpenTK::Mathematics::Vector3 Origin{};
            OpenTK::Mathematics::Vector3 Direction{};
        };

        // After a shot leaves PlayerEntity::TryFireWeapon.
        static void Fired(Entities::PlayerEntity& shooter, const FiredShot& shot);
        // The sequence of the shot this machine's own player fired this frame
        // (Fired runs before its projectiles spawn), for them to carry; 0 for
        // a shot that made no event.
        [[nodiscard]] static std::uint32_t SequenceOfShotFired(const Entities::PlayerEntity& shooter) noexcept;
        // First thing in TryFireWeapon: a copy driven by events takes up the
        // weapon and charge of its oldest unspent event.
        static void PrepareShot(Entities::PlayerEntity& shooter);
        // The event a copy is firing now.
        [[nodiscard]] static std::optional<IntentPacket::ShotEvent> FiringEvent(const Entities::PlayerEntity& shooter) noexcept;
        // The same, when its ray is one this copy can have fired -- it leaves
        // from within MaxRayOffset of where the copy stands: the ray and the
        // launch frame the shot is fired with, instead of the latest intent's.
        [[nodiscard]] static std::optional<IntentPacket::ShotEvent> FiringRay(const Entities::PlayerEntity& shooter) noexcept;
        static constexpr float MaxRayOffset = 6.0F;
        // Whether TryFireWeapon may go on to fire.
        [[nodiscard]] static bool MayFire(const Entities::PlayerEntity& shooter) noexcept;
        // Fires every event waiting for this copy, alive or just killed.
        // Returns how many left.
        static std::int32_t FireReady(Entities::PlayerEntity& shooter);
        static void Attach(IntentPacket& intent) noexcept;
        // Before the frame-order check that refuses an older intent: its
        // movement is stale, its shots are not.
        static void Receive(std::int32_t slot, const IntentPacket& intent) noexcept;
        // A shot a remote player reported, by sequence: what a hit claim
        // naming it is checked against.
        [[nodiscard]] static std::optional<IntentPacket::ShotEvent> Find(std::int32_t slot, std::uint32_t sequence) noexcept;
        // The player in the slot begins a life: what their last one left
        // unfired is abandoned. Their sequence and the ledger carry on, since
        // a sequence is never reused and a claim may still name an old shot.
        static void BeginLife(std::int32_t slot) noexcept;
        // A new occupant, or a room change: everything about the slot. Its
        // statistics move to the machine's total (Describe) and stay the
        // slot's (Stats) until a new occupant's first event arrives -- a
        // player who left before the report is still a player it describes.
        static void Forget(std::int32_t slot) noexcept;
        // What became of the slot's occupant's shot events here: the current
        // one's, or the last one's to leave if nobody has sent since.
        [[nodiscard]] static ShotQueueStats Stats(std::int32_t slot) noexcept;
        // How many shots this machine's own player has made events of.
        [[nodiscard]] static std::uint32_t Sent() noexcept { return _local.Sent(); }
        // What became of every remote shot event this machine received, and
        // what the ledger refused; nullopt before any arrived.
        [[nodiscard]] static std::optional<std::string> Describe();

    private:
        [[nodiscard]] static bool Drives(const Entities::PlayerEntity& shooter) noexcept;
        [[nodiscard]] static bool Records(const Entities::PlayerEntity& shooter) noexcept;

        inline static LocalShotLog _local{};
        inline static std::array<RemoteShotQueue, Slots> _remote{};
        inline static std::array<ShotEventLedger, Slots> _ledger{};
        // Occupants gone: still part of what this machine received.
        inline static ShotQueueStats _departed{};
        inline static std::array<ShotQueueStats, Slots> _lastDeparted{};
    };
}
