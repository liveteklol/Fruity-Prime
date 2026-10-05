#pragma once

#include "../../Formats/Enums.hpp"
#include "../../Formats/Types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace MphRead
{
    struct MessageInfo;
    class Model;
}

namespace MphRead::Entities
{
    class JumpPadEntity;
    class PlayerSpawnEntity;
}

#define MPHREAD_PLAYER_PROCESS_MEMBERS                                                         \
public:                                                                                        \
    [[nodiscard]] bool Process() override;                                                     \
    [[nodiscard]] bool ProcessPlayer();                                                        \
    void ActivateJumpPad(::MphRead::Entities::JumpPadEntity* jumpPad,                          \
        ::OpenTK::Mathematics::Vector3 vector, std::uint16_t lockTime);                        \
    void GainHealth(std::uint32_t health);                                                     \
    void GainHealth(std::int32_t health);                                                      \
    void ExitAltForm();                                                                        \
    void HandleMessage(::MphRead::MessageInfo info) override;                                  \
    void Destroy() override;                                                                   \
private:                                                                                       \
    [[nodiscard]] static bool BombCountCheck();                                                \
    void CheckSyluxBombCount();                                                                \
    void PickUpItems();                                                                        \
    void PickUpWeapon(::MphRead::ItemType itemType);                                           \
    void AnimateSpireAltAttack();                                                              \
    void UpdateSpireAltCollisionPose();                                                        \
public:                                                                                        \
    void PlayHealthPickupSfx(::MphRead::ItemType itemType);                                    \
    [[nodiscard]] std::pair<::OpenTK::Mathematics::Vector3, ::OpenTK::Mathematics::Vector3>    \
        ModSpireAltCollisionPose() const;                                                      \
private:                                                                                       \
    [[nodiscard]] bool TrySwitchForms(bool force = false);                                     \
    void UpdateAimVecs();                                                                      \
    void InitAltTransform();                                                                   \
    void UpdateAltTransform();                                                                 \
    void UpdateStinglarvaSegments();                                                           \
    void EnterAltForm();                                                                       \
    void UpdateForm(bool altForm);                                                             \
    void CreateBurnEffect();                                                                   \
    void CreateIceBreakEffectGun();                                                            \
    void CreateIceBreakEffectBiped(::MphRead::Model& model);                                   \
    void CreateIceBreakEffectAlt();                                                            \
    [[nodiscard]] std::shared_ptr<::MphRead::Entities::PlayerSpawnEntity> GetRespawnPoint();  \
    [[nodiscard]] std::int32_t GetTimeUntilRespawn();                                          \
    inline static std::optional<bool> _bombCountCheck{};                                       \
    std::int32_t _lastBombCountReport = -1;                                                    \
    inline static std::array<std::int32_t, 3> _healthPickupAmounts{30, 60, 100};              \
    static constexpr float BobWalkSpeedSquared = 0.0004F;
