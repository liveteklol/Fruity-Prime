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
        ::OpenTK::Mathematics::Vector3 vector, std::uint16_t lockTime);                            void Q3Teleport(::OpenTK::Mathematics::Vector3 position,                                           ::OpenTK::Mathematics::Vector3 facing,                                                         ::MphRead::Formats::Culling::NodeRef nodeRef, float speed);                            \
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
    void PlayHealthPickupSfx(::MphRead::ItemType itemType);                                \
    /* What picking up a timed power-up does: its timer, its sounds for */                 \
    /* this machine's player and, for a Death Alt, the morph. Without */                   \
    /* `announce`, no pickup chime: an effect put back, not picked up. */                  \
    void GrantItemEffect(::MphRead::ItemType type, std::uint16_t frames, bool announce = true);\
    /* Retimes a running power-up, quietly; 0 ends it as running out would. */             \
    void SetItemEffectFrames(::MphRead::ItemType type, std::uint16_t frames);              \
    /* Frames a power-up has left; the cloak's only while it is the item's. */             \
    [[nodiscard]] std::uint16_t ItemEffectFrames(::MphRead::ItemType type) const noexcept; \
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
