#include "Mods/Network/Gameplay/Bombs/NetBombs.hpp"

#include "Mods/Network/Bots/ServerBots.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Player/PlayerFormReplication.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Entities/BombEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <tuple>

namespace MphRead::Mods::Network
{
    static_assert(NetBombs::Slots == Entities::PlayerEntity::SlotCapacity);

    namespace
    {
        std::string FormContext(const Entities::PlayerEntity& player, std::int32_t slot)
        {
            std::string context = " local=" + player.ModFormState()
                + " localAlt=" + (player.IsAltForm() ? std::string("true") : std::string("false"));
            if (NetSession::HasRemoteState(slot))
            {
                const PlayerState& authority = NetSession::RemoteState(static_cast<std::size_t>(slot));
                const bool morphing = (authority.WeavelFlags & PlayerState::WeavelFlagMorphing) != 0;
                const bool unmorphing = (authority.WeavelFlags & PlayerState::WeavelFlagUnmorphing) != 0;
                context += " authorityAlt="
                    + ((authority.Flags & PlayerState::FlagAltForm) != 0
                        ? std::string("true") : std::string("false"))
                    + " authorityTransition=" + (morphing ? std::string("morph")
                        : unmorphing ? std::string("unmorph") : std::string("none"))
                    + " authorityProgress=" + std::to_string(authority.FormTransitionProgress())
                    + " authoritySnapshot=" + std::to_string(NetSession::AppliedSnapshotFrame());
            }
            else
            {
                context += " authority=unavailable";
            }
            if (NetSession::HasRemoteIntent(slot))
            {
                const IntentPacket& intent = NetSession::RemoteIntent(static_cast<std::size_t>(slot));
                const bool intentAlt = (intent.Buttons & IntentButtons::AltFormState)
                    != IntentButtons::None;
                context += " ownerIntentAlt=" + (intentAlt
                    ? std::string("true") : std::string("false"))
                    + " ownerIntentFrame=" + std::to_string(intent.Frame);
            }
            return context;
        }

    }

    std::array<NetBombs::HeldBombs, NetBombs::Slots> NetBombs::_owned{};
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
        const std::int32_t slot = owner.SlotIndex();
        const bool localOwner = slot == NetHooks::LocalSlot();
        const bool serverBot = NetSession::IsAuthority() && owner.IsBot() && ServerBots::Holds(slot);
        if (!NetSession::Active() || bomb == nullptr || slot < 0 || slot >= Slots || (!localOwner && !serverBot))
        {
            return;
        }
        _sequence = _sequence + 1U == 0U ? 1U : _sequence + 1U;
        bomb->ModSequence = _sequence;
        Keep(_owned[static_cast<std::size_t>(slot)], bomb, _sequence);
        NetLog::Event("[form-bomb] owner-placed slot=" + std::to_string(slot)
            + " sequence=" + std::to_string(_sequence)
            + " frame=" + std::to_string(NetSession::NetFrame())
            + FormContext(owner, slot));
    }

    void NetBombs::AttachFor(std::int32_t slot, IntentPacket& intent, std::uint32_t frame)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        intent.HasBombs = true;
        const std::uint32_t now = std::max(1U, frame);
        HeldBombs& owned = _owned[static_cast<std::size_t>(slot)];
        std::array<IntentPacket::Bomb, std::tuple_size_v<HeldBombs>> standing{};
        std::size_t standingCount = 0;
        for (Held& held : owned)
        {
            if (held.Sequence == 0)
            {
                continue;
            }
            if (const auto bomb = Standing(held))
            {
                held.Position = static_cast<OpenTK::Mathematics::Vector3>(bomb->Position);
                standing[standingCount++] = {held.Sequence, held.Position, false};
                continue;
            }
            if (held.GoneAt == 0)
            {
                held.GoneAt = now;
            }
            if (now - held.GoneAt <= GoneFrames)
            {
                standing[standingCount++] = {held.Sequence, held.Position, true};
            }
            else
            {
                held = Held{};
            }
        }
        std::sort(standing.begin(), standing.begin() + static_cast<std::ptrdiff_t>(standingCount),
            [](const IntentPacket::Bomb& a, const IntentPacket::Bomb& b)
            {
                return NetLifecycleTracker::Newer(b.Sequence, a.Sequence);
            });
        // The newest, oldest first: three standing and three just gone at most.
        const std::size_t skip = standingCount > intent.Bombs.size() ? standingCount - intent.Bombs.size() : 0;
        intent.BombsLength = 0;
        for (std::size_t i = skip; i < standingCount; ++i)
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
        if (!_remote[static_cast<std::size_t>(slot)].Receive(intent)
            || !NetSmoothing::Active())
        {
            return;
        }
        const std::uint32_t frame = NetSession::AppliedSnapshotFrame();
        if (frame == 0)
        {
            return;
        }
        std::array<NetSmoothing::TrackedPosition, IntentPacket::BombCount> positions{};
        const std::size_t count = std::min<std::size_t>(intent.BombsLength, intent.Bombs.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            const IntentPacket::Bomb& reported = intent.Bombs[i];
            positions[i] = NetSmoothing::TrackedPosition{
                reported.Sequence, reported.Position, !reported.Gone};
        }
        NetSmoothing::RecordTrackedPositions(frame, slot, std::span(positions.data(), count));
    }

    bool NetBombs::Drives(const Entities::PlayerEntity& player) noexcept
    {
        const std::int32_t slot = player.SlotIndex();
        return NetSession::Active() && slot >= 0 && slot < Slots && slot != NetHooks::LocalSlot()
            && _remote[static_cast<std::size_t>(slot)].Active();
    }

    bool NetBombs::IsRemoteReplica(const Entities::BombEntity& bomb) noexcept
    {
        const Entities::PlayerEntity* owner = bomb.Owner();
        return owner != nullptr && !NetSession::IsAuthority() && !NetSession::IsHost()
            && Drives(*owner);
    }

    std::optional<OpenTK::Mathematics::Vector3> NetBombs::ReportedPosition(
        std::int32_t slot, std::uint32_t sequence) noexcept
    {
        if (slot < 0 || slot >= Slots || sequence == 0)
        {
            return std::nullopt;
        }
        return _remote[static_cast<std::size_t>(slot)].StandingAt(sequence);
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
        for (const Held& h : copies)
        {
            const auto bomb = Standing(h);
            if (bomb == nullptr)
            {
                continue;
            }
            const std::optional<OpenTK::Mathematics::Vector3> at = _remote[s].StandingAt(h.Sequence);
            if (at.has_value())
            {
                // Keep gameplay position at the owner's latest report. The
                // shared playout clock only changes the replica's draw pose.
                bomb->ModMoveTo(*at);
            }
            const bool detonated = std::find(plan.Detonate.begin(),
                plan.Detonate.begin() + static_cast<std::ptrdiff_t>(plan.DetonateCount), h.Sequence)
                != plan.Detonate.begin() + static_cast<std::ptrdiff_t>(plan.DetonateCount);
            if (detonated)
            {
                OpenTK::Mathematics::Vector3 ignored{};
                bool present = false;
                const bool hasPlayout = NetSmoothing::SampleTrackedPosition(
                    player.SlotIndex(), h.Sequence, ignored, present);
                if (!hasPlayout || !present)
                {
                    bomb->SetCountdown(0);
                }
            }
        }
        for (std::size_t i = 0; i < plan.LayCount; ++i)
        {
            const IntentPacket::Bomb& reported = plan.Lay[i];
            OpenTK::Mathematics::Vector3 smoothed{};
            bool present = false;
            const bool hasSample = NetSmoothing::SampleTrackedPosition(
                player.SlotIndex(), reported.Sequence, smoothed, present);
            const std::uint32_t frame = NetSession::NetFrame();
            const IntentPacket* ownerIntent = nullptr;
            if (NetSession::HasRemoteIntent(player.SlotIndex()))
            {
                ownerIntent = &NetSession::RemoteIntent(s);
            }
            if (_lastAltActionCue[s] != reported.Sequence)
            {
                const PlayerState* authorityState = nullptr;
                if (NetSession::HasRemoteState(player.SlotIndex()))
                {
                    authorityState = &NetSession::RemoteState(s);
                }
                static_cast<void>(PlayerFormReplication::AdvanceForAltFormAction(
                    player, ownerIntent, authorityState, frame));
                _lastAltActionCue[s] = reported.Sequence;
            }
            const std::shared_ptr<Entities::BombEntity> bomb = player.ModPlaceReportedBomb(reported.Position);
            if (bomb == nullptr)
            {
                // Not yet (a Lockjaw chain still going off): next step, in order.
                break;
            }
            bomb->ModSequence = reported.Sequence;
            Keep(copies, bomb, reported.Sequence);
            _remote[s].Laid(reported.Sequence);
            NetLog::Event("[form-bomb] replica-placed slot=" + std::to_string(player.SlotIndex())
                + " sequence=" + std::to_string(reported.Sequence)
                + " frame=" + std::to_string(NetSession::NetFrame())
                + " gone=" + (reported.Gone ? std::string("true") : std::string("false"))
                + FormContext(player, player.SlotIndex()));
            if (reported.Gone && (!hasSample || !present))
            {
                // The playout has reached Gone, or no track was available to wait on.
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
        _owned[static_cast<std::size_t>(slot)] = HeldBombs{};
        _lastAltActionCue[static_cast<std::size_t>(slot)] = 0;
    }
}
