#include "Mods/Network/Gameplay/StatusEffects/BurnStatusEffectPolicy.hpp"

#include "Entities/Players/PlayerEntity.hpp"

namespace MphRead::Mods::Network
{
    void BurnStatusEffectPolicy::ApplyAuthoritativeHit(Entities::PlayerEntity& victim,
        Entities::PlayerEntity& source, std::uint32_t launchFrame)
    {
        victim.ModIgnite(source, launchFrame);
    }

    bool BurnStatusEffectPolicy::IsAuthorityOnlyDamageTick(Entities::DamageFlags flags) noexcept
    {
        return (static_cast<std::int32_t>(flags)
            & static_cast<std::int32_t>(Entities::DamageFlags::Burn)) != 0;
    }

    std::uint16_t BurnStatusEffectPolicy::Capture(const Entities::PlayerEntity& player)
    {
        return player.ModBurningFrames();
    }

    std::uint16_t BurnStatusEffectPolicy::Current(const Entities::PlayerEntity& player)
    {
        return player.ModBurningFrames();
    }

    void BurnStatusEffectPolicy::Start(Entities::PlayerEntity& player, std::uint16_t frames)
    {
        player.ModMirrorBurningFrames(frames);
    }

    void BurnStatusEffectPolicy::Update(Entities::PlayerEntity& player, std::uint16_t frames)
    {
        player.ModMirrorBurningFrames(frames);
    }

    void BurnStatusEffectPolicy::End(Entities::PlayerEntity& player)
    {
        player.ModMirrorBurningFrames(0);
    }
}
