#pragma once

#include "../../Formats/Culling.hpp"
#include "../../Formats/Enums.hpp"
#include "../../Formats/Types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace MphRead
{
    class Scene;
}

#define MPHREAD_PLAYER_ENTITY_NET_AIM_MEMBERS \
public: \
    [[nodiscard]] OpenTK::Mathematics::Vector3 ModMuzzlePos() const noexcept { return _muzzlePos; } \
    void ModResetNetworkHistory() noexcept { _networkPositionHistoryCount = 0; } \
    void ModRecordNetworkPosition(std::uint32_t frame); \
    [[nodiscard]] bool ModGetNetworkPosition( \
        std::uint32_t frame, OpenTK::Mathematics::Vector3& position); \
 \
    [[nodiscard]] OpenTK::Mathematics::Vector3 ModGunVector() const; \
    void ModRefreshNetworkAim(); \
    void ModSetAim(OpenTK::Mathematics::Vector3 aim); \
 \
    [[nodiscard]] static Formats::Culling::NodeRef ModWalkNodeRef( \
        MphRead::Scene& scene, \
        Formats::Culling::NodeRef current, \
        OpenTK::Mathematics::Vector3 previous, \
        OpenTK::Mathematics::Vector3 position); \
 \
    void ModPlaceAt(OpenTK::Mathematics::Vector3 position); \
    [[nodiscard]] bool ModPlacementBelongsHere(OpenTK::Mathematics::Vector3 position); \
    [[nodiscard]] std::optional<OpenTK::Mathematics::Vector3> ModSpawnFacingAt( \
        OpenTK::Mathematics::Vector3 position); \
    void ModSetSpawnFacing(OpenTK::Mathematics::Vector3 facing); \
    void ModRefreshNodeRef(OpenTK::Mathematics::Vector3 previousPosition); \
    [[nodiscard]] bool ModNodeUnresolved() const noexcept; \
 \
    void ModLogCollisionRange(); \
    void ModSetFacing(OpenTK::Mathematics::Vector3 facing); \
    void ModSetSpectating(bool value); \
    [[nodiscard]] bool ModInPlay() const; \
    [[nodiscard]] bool ModIsInPlay() const; \
 \
    void ModNetSpawn( \
        OpenTK::Mathematics::Vector3 position, \
        OpenTK::Mathematics::Vector3 facing); \
    [[nodiscard]] static Formats::Culling::NodeRef ModSpawnNodeRef( \
        MphRead::Scene& scene, OpenTK::Mathematics::Vector3 position); \
 \
    [[nodiscard]] std::pair<float, float> ModAimDeltaTowards( \
        OpenTK::Mathematics::Vector3 target); \
    [[nodiscard]] OpenTK::Mathematics::Vector3 ModAimTarget() const; \
 \
    void ModSetHunter(MphRead::Hunter hunter); \
    [[nodiscard]] bool ModDamageIndicatorActive() const; \
 \
    void ModStartFormSwitch(); \
    void ModForceForm(bool altForm); \
    void ModForceWeavelState(bool desiredAlt, bool desiredTurretActive, \
        std::optional<std::int32_t> desiredTurretHealth = std::nullopt); \
    void ModApplyWeavelState(bool desiredAlt, bool turretActive, std::int32_t turretHealth, \
        OpenTK::Mathematics::Vector3 turretPosition, bool turretGrounded); \
    /* The authority's word on this machine's own turret: WeavelOwnedTurret. */ \
    /* A bomb its owner reports, placed where it stands: NetBombs. Null when it */ \
    /* cannot be placed yet (a Lockjaw chain of three still going off). */ \
    std::shared_ptr<BombEntity> ModPlaceReportedBomb(OpenTK::Mathematics::Vector3 position); \
    void ModApplyOwnWeavelTurret(bool authorityHeadingAlt, bool authorityTurretActive, std::int32_t turretHealth); \
private: \
    void FinalizeWeavelForm(bool desiredAlt); \
public: \
 \
    void ModSetWeapon(MphRead::BeamType weapon); \
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> ModAmmo() const; \
    [[nodiscard]] std::int32_t ModAmmoCap() const { return _ammoMax[UA]; } \
    [[nodiscard]] std::int32_t ModBoostDamage() const noexcept { return _boostDamage; } \
    void ModSetShotState(std::int32_t chargeLevel, std::int32_t boostDamage, bool doubleDamage); \
    [[nodiscard]] ::MphRead::Mods::Input::NativeTouchState::Reported ModTouchReport() const noexcept \
    { \
        return _input.TouchSample().Report(); \
    } \
    void ModSetReportedTouch(const ::MphRead::Mods::Input::NativeTouchState::Reported& reported, \
        std::uint16_t generation, std::uint16_t life); \
    void ModSetAmmo(std::int32_t ua, std::int32_t missiles); \
    void ModSetZoom(bool zoomed); \
 \
    void ModArmAffinityWeapon(); \
    void ModArmWeapon(MphRead::BeamType beam); \
    void ModArmZoomWeapon(); \
 \
    void ModApplyScriptAim(float deltaX, float deltaY); \
    [[nodiscard]] std::string ModWeaponState() const; \
    [[nodiscard]] std::int32_t ModChargeLevel() const; \
    [[nodiscard]] bool ModChargeReady() const; \
 \
    [[nodiscard]] bool ModFrozen() const; \
    void ModSetFrozen(bool frozen); \
    void ModRefreshVolume(); \
    void ModRefreshAttachedEffects(); \
    [[nodiscard]] bool ModFireShotEvent(); \
    [[nodiscard]] bool ModBurning() const; \
    [[nodiscard]] bool ModDisrupted() const; \
    void ModSetDisrupted(bool disrupted); \
    void ModSetBurning(bool burning); \
    /* Set on fire by a claimed hit: burning as ModSetBurning(true), and the */ \
    /* burner recorded, so the burn's damage is theirs -- their kill, their */ \
    /* claims -- and not a burn with nobody behind it. */ \
    void ModIgnite(PlayerEntity& by, std::uint32_t launchFrame); \
    [[nodiscard]] bool ModCanZoom() const; \
 \
    [[nodiscard]] std::string ModFormState() const; \
    void ModRepairVectors(); \
 \
    void ModNetDie(); \
    [[nodiscard]] std::pair<std::int32_t, float> ModScoreboardSize() const; \
    [[nodiscard]] bool ModCanBeHurt() const; \
 \
    void ModNoteInput(); \
 \
private: \
    static constexpr std::int32_t NetworkHistoryLength = 120; \
    static constexpr float _nodeWalkStepMax = 4.0F; \
 \
    std::array<OpenTK::Mathematics::Vector3, NetworkHistoryLength> \
        _networkPositionHistory{}; \
    std::array<std::uint32_t, NetworkHistoryLength> _networkPositionFrames{}; \
    std::int32_t _networkPositionHistoryCount = 0; \
 \
    bool _modNodeUnresolved = false; \
 \
    OpenTK::Mathematics::Vector3 _modLastGoodFacing{0.0F, 0.0F, -1.0F}; \
    OpenTK::Mathematics::Vector3 _modLastGoodGunVec{0.0F, 0.0F, -1.0F}; \
    OpenTK::Mathematics::Vector3 _modLastGoodPosition{}; \
 \
    [[nodiscard]] OpenTK::Mathematics::Vector3 ModAimVectorTowards( \
        OpenTK::Mathematics::Vector3 target) const; \
    [[nodiscard]] static bool Finite(OpenTK::Mathematics::Vector3 value) noexcept; \
    [[nodiscard]] std::shared_ptr<PlayerSpawnEntity> ModNearestSpawn( \
        OpenTK::Mathematics::Vector3 position, bool& any); \
 \
    void ApplyModAim();

#ifndef MPHREAD_PLAYER_ENTITY_CANONICAL_HEADER
#include "../../Entities/Players/PlayerEntity.hpp"

namespace MphRead::Entities::PlayerEntityNetAimDetail
{
}

namespace MphRead::Entities::Detail
{
    using namespace ::MphRead::Entities::PlayerEntityNetAimDetail;
}
#endif
