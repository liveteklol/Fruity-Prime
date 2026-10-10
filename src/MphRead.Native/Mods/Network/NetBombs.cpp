#include "NetBombs.hpp"

#include "NetHooks.hpp"
#include "NetLifecycleTracker.hpp"
#include "NetSession.hpp"
#include "../../Entities/BombEntity.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"

#include <algorithm>
#include <vector>

namespace MphRead::Mods::Network
{
    static_assert(NetBombs::Slots == Entities::PlayerEntity::SlotCapacity);

    NetBombs::HeldBombs NetBombs::_own{};
    std::array<NetBombs::HeldBombs, NetBombs::Slots> NetBombs::_copies{};

    std::shared_ptr<Entities::BombEntity> NetBombs::Standing(const Held& held) noexcept
    {
        std::shared_ptr<Entities::BombEntity> bomb = held.Bomb.lock();
        // Pooled: a bomb destroyed, or placed again for somebody else, no
        // longer carries the sequence it was kept under.
        return bomb != nullptr && held.Sequence != 0 && bomb->ModSequence == held.Sequence ? bomb : nullptr;
    }

    void NetBombs::Keep(HeldBombs& held, const std::shared_ptr<Entities::BombEntity>& bomb, std::uint32_t sequence)
    {
        // A free entry: one never used, or one whose bomb is gone and no
        // longer reported.
        auto slot = std::find_if(held.begin(), held.end(),
            [](const Held& h) { return h.Sequence == 0 || (Standing(h) == nullptr && h.GoneAt == 0); });
        if (slot == held.end())
        {
            slot = std::min_element(held.begin(), held.end(), [](const Held& a, const Held& b)
                {
                    return NetLifecycleTracker::Newer(b.Sequence, a.Sequence);
                });
        }
        *slot = Held{bomb, sequence, static_cast<OpenTK::Mathematics::Vector3>(bomb->Position), 0};
    }

    void NetBombs::Laid(Entities::PlayerEntity& owner, const std::shared_ptr<Entities::BombEntity>& bomb)
    {
        if (!NetSession::Active() || bomb == nullptr || owner.SlotIndex() < 0
            || owner.SlotIndex() != NetHooks::LocalSlot())
        {
            return;
        }
        _sequence = _sequence + 1U == 0U ? 1U : _sequence + 1U;
        bomb->ModSequence = _sequence;
        Keep(_own, bomb, _sequence);
    }

    void NetBombs::Attach(IntentPacket& intent)
    {
        intent.HasBombs = true;
        const std::uint32_t now = std::max(1U, NetSession::NetFrame());
        std::vector<IntentPacket::Bomb> standing;
        for (Held& held : _own)
        {
            if (held.Sequence == 0)
            {
                continue;
            }
            if (const auto bomb = Standing(held))
            {
                held.Position = static_cast<OpenTK::Mathematics::Vector3>(bomb->Position);
                standing.push_back({held.Sequence, held.Position, false});
                continue;
            }
            if (held.GoneAt == 0)
            {
                held.GoneAt = now;
            }
            if (now - held.GoneAt <= GoneFrames)
            {
                standing.push_back({held.Sequence, held.Position, true});
            }
            else
            {
                held = Held{};
            }
        }
        std::sort(standing.begin(), standing.end(), [](const IntentPacket::Bomb& a, const IntentPacket::Bomb& b)
            {
                return NetLifecycleTracker::Newer(b.Sequence, a.Sequence);
            });
        // The newest, oldest first: three standing and three just gone at most.
        const std::size_t skip = standing.size() > intent.Bombs.size() ? standing.size() - intent.Bombs.size() : 0;
        intent.BombsLength = 0;
        for (std::size_t i = skip; i < standing.size(); ++i)
        {
            intent.Bombs[intent.BombsLength++] = standing[i];
        }
    }

    void NetBombs::Receive(std::int32_t slot, const IntentPacket& intent) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        _remote[static_cast<std::size_t>(slot)].Receive(intent);
    }

    bool NetBombs::Drives(const Entities::PlayerEntity& player) noexcept
    {
        const std::int32_t slot = player.SlotIndex();
        return NetSession::Active() && slot >= 0 && slot < Slots && slot != NetHooks::LocalSlot()
            && !player.IsBot() && _remote[static_cast<std::size_t>(slot)].Active();
    }

    void NetBombs::Reconcile(Entities::PlayerEntity& player)
    {
        if (!Drives(player))
        {
            return;
        }
        const auto s = static_cast<std::size_t>(player.SlotIndex());
        HeldBombs& copies = _copies[s];
        std::array<std::uint32_t, std::tuple_size_v<HeldBombs>> held{};
        std::size_t heldCount = 0;
        for (const Held& h : copies)
        {
            if (Standing(h) != nullptr)
            {
                held[heldCount++] = h.Sequence;
            }
        }
        const RemoteBombState::Plan plan = _remote[s].Reconcile(std::span(held.data(), heldCount));
        for (std::size_t i = 0; i < plan.DetonateCount; ++i)
        {
            for (const Held& h : copies)
            {
                if (h.Sequence == plan.Detonate[i])
                {
                    if (const auto bomb = Standing(h))
                    {
                        bomb->SetCountdown(0);
                    }
                }
            }
        }
        for (const Held& h : copies)
        {
            const auto bomb = Standing(h);
            const std::optional<OpenTK::Mathematics::Vector3> at = bomb != nullptr
                ? _remote[s].StandingAt(h.Sequence) : std::nullopt;
            if (!at.has_value())
            {
                continue;
            }
            const OpenTK::Mathematics::Vector3 here = static_cast<OpenTK::Mathematics::Vector3>(bomb->Position);
            const float distance = OpenTK::Mathematics::Length(*at - here);
            if (distance > SnapDistance)
            {
                bomb->ModMoveTo(*at);
            }
            else if (distance > StillDistance)
            {
                bomb->ModMoveTo(here + OpenTK::Mathematics::Scale(*at - here, Follow));
            }
        }
        for (std::size_t i = 0; i < plan.LayCount; ++i)
        {
            const IntentPacket::Bomb& reported = plan.Lay[i];
            const std::shared_ptr<Entities::BombEntity> bomb = player.ModPlaceReportedBomb(reported.Position);
            if (bomb == nullptr)
            {
                // Not yet (a Lockjaw chain still going off): next step, in order.
                break;
            }
            bomb->ModSequence = reported.Sequence;
            Keep(copies, bomb, reported.Sequence);
            _remote[s].Laid(reported.Sequence);
            if (reported.Gone)
            {
                // Gone already on the owner's: here it goes off as it lands.
                bomb->SetCountdown(0);
            }
        }
    }

    bool NetBombs::DetonatesOnContact(const Entities::BombEntity& bomb) noexcept
    {
        const Entities::PlayerEntity* owner = bomb.Owner();
        if (!NetSession::Active() || NetSession::IsAuthority() || owner == nullptr)
        {
            return true;
        }
        return !Drives(*owner);
    }

    void NetBombs::Forget(std::int32_t slot) noexcept
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        _remote[static_cast<std::size_t>(slot)].Reset();
        _copies[static_cast<std::size_t>(slot)] = HeldBombs{};
        if (slot == NetHooks::LocalSlot())
        {
            _own = HeldBombs{};
        }
    }
}
