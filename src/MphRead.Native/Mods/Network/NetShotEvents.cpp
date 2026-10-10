#include "NetShotEvents.hpp"

#include "NetHooks.hpp"
#include "NetSession.hpp"
#include "NetUnlagged.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <optional>

namespace MphRead::Mods::Network
{
    static_assert(NetShotEvents::Slots == Entities::PlayerEntity::SlotCapacity);

    namespace
    {
        [[nodiscard]] bool Continuous(const Entities::PlayerEntity& shooter) noexcept
        {
            const auto& weapon = NativeRuntime::RequireReference(shooter.EquipInfo()).Weapon;
            return weapon != nullptr && ::MphRead::TestFlag(weapon->Flags, ::MphRead::WeaponFlags::Continuous);
        }
    }

    bool NetShotEvents::Drives(const Entities::PlayerEntity& shooter) noexcept
    {
        const std::int32_t slot = shooter.SlotIndex();
        return NetSession::Active() && slot >= 0 && slot < Slots && slot != NetHooks::LocalSlot()
            && !shooter.IsBot() && _remote[static_cast<std::size_t>(slot)].Active();
    }

    bool NetShotEvents::Records(const Entities::PlayerEntity& shooter) noexcept
    {
        // The authority's own shots are resolved where they are fired.
        return NetSession::Active() && !NetSession::IsAuthority()
            && shooter.SlotIndex() == NetHooks::LocalSlot() && shooter.SlotIndex() >= 0;
    }

    void NetShotEvents::Fired(Entities::PlayerEntity& shooter, const FiredShot& shot)
    {
        if (shot.Continuous || !NetSession::Active())
        {
            return;
        }
        if (Records(shooter))
        {
            IntentPacket::ShotEvent event{};
            event.Frame = std::max(1U, NetSession::NetFrame());
            event.WeaponId = static_cast<std::uint8_t>(shot.Weapon);
            event.Charge = static_cast<std::uint8_t>(std::min<std::uint16_t>(
                NativeRuntime::RequireReference(shooter.EquipInfo()).ChargeLevel, 0xFF));
            // The same world the projectile was stamped with as it spawned.
            event.AckFrame = NetUnlagged::LaunchFrameFor(shooter);
            event.Origin = shot.Origin;
            event.Direction = shot.Direction;
            static_cast<void>(_local.Record(event));
        }
        else if (Drives(shooter))
        {
            _remote[static_cast<std::size_t>(shooter.SlotIndex())].Consume();
        }
    }

    std::uint32_t NetShotEvents::SequenceOfShotFired(const Entities::PlayerEntity& shooter) noexcept
    {
        const IntentPacket::ShotEvent* latest = Records(shooter) ? _local.Latest() : nullptr;
        return latest != nullptr && latest->Frame == std::max(1U, NetSession::NetFrame()) ? latest->Sequence : 0U;
    }

    void NetShotEvents::PrepareShot(Entities::PlayerEntity& shooter)
    {
        const std::optional<IntentPacket::ShotEvent> event = FiringEvent(shooter);
        if (!event.has_value())
        {
            return;
        }
        const auto weapon = static_cast<::MphRead::BeamType>(event->WeaponId);
        if (weapon != shooter.CurrentWeapon())
        {
            shooter.ModSetWeapon(weapon);
        }
        // After the switch, which resets it: the charge is the shot's.
        NativeRuntime::RequireReference(shooter.EquipInfo()).ChargeLevel = event->Charge;
    }

    std::optional<IntentPacket::ShotEvent> NetShotEvents::FiringEvent(const Entities::PlayerEntity& shooter) noexcept
    {
        if (!Drives(shooter))
        {
            return std::nullopt;
        }
        return _remote[static_cast<std::size_t>(shooter.SlotIndex())].Next();
    }

    std::optional<IntentPacket::ShotEvent> NetShotEvents::FiringRay(const Entities::PlayerEntity& shooter) noexcept
    {
        std::optional<IntentPacket::ShotEvent> event = FiringEvent(shooter);
        if (!event.has_value() || !event->HasRay()
            || (event->Origin - static_cast<OpenTK::Mathematics::Vector3>(shooter.Position)).LengthSquared()
                > MaxRayOffset * MaxRayOffset)
        {
            return std::nullopt;
        }
        return event;
    }

    bool NetShotEvents::MayFire(const Entities::PlayerEntity& shooter) noexcept
    {
        return !Drives(shooter) || Continuous(shooter) || FiringEvent(shooter).has_value();
    }

    std::int32_t NetShotEvents::FireReady(Entities::PlayerEntity& shooter)
    {
        // A beam weapon is fired standing: an event met in the ball waits for
        // the copy to stand, or goes stale.
        if (!Drives(shooter) || shooter.IsAltForm() || shooter.IsMorphing())
        {
            return 0;
        }
        std::int32_t fired = 0;
        for (std::size_t i = 0; i < RemoteShotQueue::Capacity && FiringEvent(shooter).has_value(); ++i)
        {
            if (!shooter.ModFireShotEvent())
            {
                break;
            }
            ++fired;
        }
        return fired;
    }

    void NetShotEvents::Attach(IntentPacket& intent) noexcept
    {
        _local.Fill(intent);
    }

    void NetShotEvents::Receive(std::int32_t slot, const IntentPacket& intent) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto s = static_cast<std::size_t>(slot);
        if (intent.ShotHistoryLength > 0)
        {
            _lastDeparted[s] = {};
        }
        _remote[s].Receive(intent);
        for (std::size_t i = 0; i < intent.ShotHistoryLength && i < intent.ShotHistory.size(); ++i)
        {
            _ledger[s].Note(intent.ShotHistory[i]);
        }
    }

    std::optional<IntentPacket::ShotEvent> NetShotEvents::Find(std::int32_t slot, std::uint32_t sequence) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return std::nullopt;
        }
        return _ledger[static_cast<std::size_t>(slot)].Find(sequence);
    }

    ShotQueueStats NetShotEvents::Stats(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return {};
        }
        const auto s = static_cast<std::size_t>(slot);
        const ShotQueueStats current = _remote[s].Stats();
        return current.Received == 0 && current.Gaps == 0 ? _lastDeparted[s] : current;
    }

    std::optional<std::string> NetShotEvents::Describe()
    {
        ShotQueueStats total = _departed;
        std::uint64_t staleRefused = 0;
        std::uint64_t conflicts = 0;
        for (std::size_t s = 0; s < _remote.size(); ++s)
        {
            total += _remote[s].Stats();
            staleRefused += _ledger[s].StaleRefused();
            conflicts += _ledger[s].Conflicts();
        }
        if (total.Received == 0 && total.Gaps == 0)
        {
            return std::nullopt;
        }
        return "shot events (received): " + std::to_string(total.Received) + " received -- "
            + std::to_string(total.Fired) + " fired (" + std::to_string(total.OutOfOrder) + " after a newer one), "
            + std::to_string(total.Stale) + " stale, "
            + std::to_string(total.Overflow) + " pushed out, "
            + std::to_string(total.Abandoned) + " abandoned with a life, "
            + std::to_string(total.Waiting) + " waiting (" + std::to_string(total.Overdue) + " overdue); "
            + std::to_string(total.Gaps) + " skipped by a newer one -- "
            + std::to_string(total.Recovered) + " recovered late, "
            + std::to_string(total.Lost) + " never arrived, "
            + std::to_string(total.Pending) + " still awaited; ledger: "
            + std::to_string(staleRefused) + " older shots refused, "
            + std::to_string(conflicts) + " contradicting a kept shot";
    }

    void NetShotEvents::BeginLife(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        _remote[static_cast<std::size_t>(slot)].BeginLife();
        if (slot == NetHooks::LocalSlot())
        {
            _local.Reset();
        }
    }

    void NetShotEvents::Forget(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const ShotQueueStats departed = _remote[static_cast<std::size_t>(slot)].Retire();
        if (departed.Received != 0 || departed.Gaps != 0)
        {
            _departed += departed;
            _lastDeparted[static_cast<std::size_t>(slot)] = departed;
        }
        _ledger[static_cast<std::size_t>(slot)].Reset();
        if (slot == NetHooks::LocalSlot())
        {
            _local.Reset();
        }
    }
}
