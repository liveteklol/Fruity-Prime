#include "Mods/Network/Gameplay/StatusEffects/DisruptStatusEffectPolicy.hpp"

#include "Entities/Players/PlayerEntity.hpp"

namespace MphRead::Mods::Network
{
    void DisruptStatusEffectPolicy::ApplyAuthoritativeHit(Entities::PlayerEntity& victim)
    {
        victim.ModSetDisrupted(true);
    }

    std::uint16_t DisruptStatusEffectPolicy::Capture(const Entities::PlayerEntity& player)
    {
        return player.ModDisruptedFrames();
    }

    std::uint16_t DisruptStatusEffectPolicy::Current(const Entities::PlayerEntity& player)
    {
        return player.ModDisruptedFrames();
    }

    void DisruptStatusEffectPolicy::Start(Entities::PlayerEntity& player, std::uint16_t frames)
    {
        player.ModMirrorDisruptedFrames(frames);
    }

    void DisruptStatusEffectPolicy::Update(Entities::PlayerEntity& player, std::uint16_t frames)
    {
        player.ModMirrorDisruptedFrames(frames);
    }

    void DisruptStatusEffectPolicy::End(Entities::PlayerEntity& player)
    {
        player.ModMirrorDisruptedFrames(0);
    }
}
