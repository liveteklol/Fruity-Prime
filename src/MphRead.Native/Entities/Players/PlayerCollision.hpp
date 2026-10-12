#pragma once

#include "../../Formats/CollisionDetection.hpp"

#include <cstdint>
#include <memory>

namespace MphRead::Entities
{
    struct DamageResult
    {
        bool TakeDamage = false;
        std::uint32_t Damage = 0;
    };
}

namespace MphRead::Entities
{
    class DoorEntity;
    class EnemyInstanceEntity;
    class PlayerEntity;
}

#define MPHREAD_PLAYER_COLLISION_MEMBERS \
public: \
    [[nodiscard]] bool CheckAltAttackHitEnemy1( \
        ::MphRead::Entities::EnemyInstanceEntity* target); \
    [[nodiscard]] bool CheckAltAttackHitEnemy2( \
        ::MphRead::Entities::EnemyInstanceEntity* target); \
    void HandleCollision(::MphRead::Formats::CollisionResult result); \
    /* What it stood on at its last collision check (a mover carries it). */ \
    [[nodiscard]] const std::shared_ptr<::MphRead::Formats::Collision::EntityCollision>& StandingEntCol() const noexcept \
    { \
        return _standingEntCol; \
    } \
private: \
    [[nodiscard]] bool DialancheHitsVolume(const ::MphRead::CollisionVolume& volume) const; \
    [[nodiscard]] std::uint16_t DialanchePlayerDamage() const; \
    void CheckDialanchePlayerHit(::MphRead::Entities::PlayerEntity& victim, bool halfturret); \
    void CheckPlayerCollision(); \
    static void CheckAltAttackHit1( \
        ::MphRead::Entities::PlayerEntity* attacker, \
        ::MphRead::Entities::PlayerEntity* target, bool halfturret); \
    static void CheckAltAttackHit2( \
        ::MphRead::Entities::PlayerEntity* attacker, \
        ::MphRead::Entities::PlayerEntity* target, bool halfturret); \
    void AltAttackHitDoor(::MphRead::Entities::DoorEntity* door); \
    void CheckCollision(); \
    std::shared_ptr<::MphRead::Formats::Collision::EntityCollision> _collidedEntCol{}; \
    std::shared_ptr<::MphRead::Formats::Collision::EntityCollision> _standingEntCol{};
