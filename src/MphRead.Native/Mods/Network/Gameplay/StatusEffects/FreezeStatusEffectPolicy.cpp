#include "Mods/Network/Gameplay/StatusEffects/FreezeStatusEffectPolicy.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    std::uint16_t FreezeStatusEffectPolicy::Capture(const Entities::PlayerEntity& player)
    {
        return player.ModFrozenFrames();
    }

    std::uint16_t FreezeStatusEffectPolicy::Current(const Entities::PlayerEntity& player)
    {
        return player.ModFrozenFrames();
    }

    void FreezeStatusEffectPolicy::ApplyAuthoritativeHit(Entities::PlayerEntity& victim)
    {
        victim.ModSetFrozen(true);
    }

    void FreezeStatusEffectPolicy::Start(Entities::PlayerEntity& player, std::int32_t slot,
        std::uint16_t frames)
    {
        player.ModMirrorFrozen(frames);
        // The authority has confirmed the predicted freeze.
        ForgetSlot(slot);
    }

    void FreezeStatusEffectPolicy::Update(Entities::PlayerEntity& player, std::int32_t slot,
        std::uint16_t frames)
    {
        player.ModMirrorFrozen(frames);
        ForgetSlot(slot);
    }

    void FreezeStatusEffectPolicy::End(Entities::PlayerEntity& player)
    {
        player.ModMirrorFrozen(0);
    }

    bool FreezeStatusEffectPolicy::AwaitingAuthority(std::int32_t slot)
    {
        if (!_predictions.HasPrediction(slot))
        {
            return false;
        }
        const std::int32_t local = NetSession::LocalSlot();
        const std::int32_t ping = SessionSlots().PingAt(local);
        const auto roundTrip = static_cast<std::uint32_t>(std::max(0, (ping * 60 + 999) / 1000));
        return _predictions.Awaiting(slot, NetSession::NetFrame(), roundTrip);
    }

    void FreezeStatusEffectPolicy::NotePredicted(std::int32_t slot)
    {
        _predictions.NotePredicted(slot, NetSession::NetFrame());
    }

    void FreezeStatusEffectPolicy::ForgetSlot(std::int32_t slot)
    {
        _predictions.ForgetSlot(slot);
    }

    void FreezeStatusEffectPolicy::Reset()
    {
        _predictions.Reset();
    }
}
