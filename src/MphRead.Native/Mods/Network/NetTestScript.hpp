#pragma once

#include "../../Formats/Types.hpp"

#include <optional>
#include <string>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace MphRead::Entities
{
    class Keybind;
    class PlayerControls;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    enum class TestPhase : std::int32_t
    {
        Idle,
        Walk,
        Jump,
        Turn,
        Shoot,
        SwitchWeapons,
        Charge,
        MorphA,
        AltAttackA,
        MorphB,
        AltAttackB,
        Unmorph,
        Zoom,
        Afflict,
        Duel,
        SelfDestruct
    };

    // TestPhase.ToString().
    [[nodiscard]] std::string ToString(TestPhase value);

    class NetTestScript final
    {
    public:
        NetTestScript() = delete;
        NetTestScript(const NetTestScript&) = delete;
        NetTestScript& operator=(const NetTestScript&) = delete;
        NetTestScript(NetTestScript&&) = delete;
        NetTestScript& operator=(NetTestScript&&) = delete;

        [[nodiscard]] static double PhaseSeconds() noexcept;
        static void SetPhaseSeconds(double value) noexcept;
        [[nodiscard]] static std::int32_t PhaseCount() noexcept;

        [[nodiscard]] static bool Enabled() noexcept;
        static void SetEnabled(bool value) noexcept;

        [[nodiscard]] static float AimDeltaX() noexcept;
        [[nodiscard]] static float AimDeltaY() noexcept;
        [[nodiscard]] static std::int32_t FramesOnTarget() noexcept;

        [[nodiscard]] static TestPhase Phase();

        static void Reset();
        static void ApplyOffline(
            std::shared_ptr<Entities::PlayerEntity> player,
            std::int32_t slot,
            std::int32_t frame);
        static void HoldFire(
            std::shared_ptr<Entities::PlayerEntity> player,
            bool down);
        static void LayBombs(
            std::shared_ptr<Entities::PlayerEntity> player,
            std::int32_t frame);
        static void Rest(
            std::shared_ptr<Entities::PlayerEntity> player,
            bool wantBiped);
        static void WalkForward(std::shared_ptr<Entities::PlayerEntity> player);
        static void Apply(std::shared_ptr<Entities::PlayerEntity> player);

    private:
        [[nodiscard]] static double ReadPhaseSeconds();
        // MPHREAD_PHASE=MorphA holds the tour on that one phase: a scripted
        // player doing one thing for as long as a live test needs it (a
        // Weavel that stays a turret to be shot at).
        [[nodiscard]] static std::optional<TestPhase> ReadPinnedPhase();
        // The server's match clock, carried forward between the MatchState
        // packets that set it (one a second, and lossy), so every client's
        // tour turns the page on the same frame give or take a trip.
        [[nodiscard]] static double ServerElapsed(float received) noexcept;

        static void Drive(const std::shared_ptr<Entities::PlayerEntity>& player);
        [[nodiscard]] static bool Settled(Entities::PlayerEntity& player);
        [[nodiscard]] static bool Even() noexcept;
        static void MorphOrShoot(
            Entities::PlayerEntity& player,
            Entities::PlayerControls& controls,
            bool morphing);
        static void AltAttackOrShoot(
            Entities::PlayerEntity& player,
            bool attacking,
            bool onTarget);
        static void Clear(Entities::PlayerControls& controls);
        static void Finish(
            Entities::PlayerEntity& player,
            Entities::PlayerControls& controls);
        [[nodiscard]] static bool AimAt(
            Entities::PlayerEntity& player,
            const std::shared_ptr<Entities::PlayerEntity>& target);
        static void Square(Entities::PlayerControls& controls);
        static void Duel(
            Entities::PlayerEntity& player,
            Entities::PlayerControls& controls,
            const std::shared_ptr<Entities::PlayerEntity>& target,
            bool onTarget,
            bool charged = false);
        [[nodiscard]] static std::shared_ptr<Entities::PlayerEntity> FindTarget(
            const std::shared_ptr<Entities::PlayerEntity>& self);
        static void Hold(Entities::Keybind& bind, bool down);
        static void SelfDestruct(Entities::PlayerEntity& player, Entities::PlayerControls& c);
        static constexpr ::MphRead::BeamType SelfDestructBeam = ::MphRead::BeamType::Magmaul;
        // MPHREAD_FEET_MISSILE: the self-destruct phase walks forward firing
        // uncharged Missiles at its own feet instead -- a splash on its own
        // shooter the frame it is fired, which a Magmaul's bounce is not.
        static void FeetMissile(Entities::PlayerEntity& player, Entities::PlayerControls& c, bool aimed);

        static constexpr float TurnRate = 6.0F;
        static constexpr float FiringCone = 6.0F;
        static constexpr float PreferredRange = 4.0F;

        static double _phaseSeconds;
        static std::optional<TestPhase> _pinnedPhase;
        inline static const std::array<TestPhase, 16> _order{
            TestPhase::Idle,
            TestPhase::Walk,
            TestPhase::Jump,
            TestPhase::Turn,
            TestPhase::Shoot,
            TestPhase::SwitchWeapons,
            TestPhase::Charge,
            TestPhase::MorphA,
            TestPhase::AltAttackA,
            TestPhase::MorphB,
            TestPhase::AltAttackB,
            TestPhase::Unmorph,
            TestPhase::Zoom,
            TestPhase::Afflict,
            TestPhase::Duel,
            TestPhase::SelfDestruct
        };

        inline static bool _enabled = false;
        inline static float _serverElapsed = -1.0F;
        inline static std::uint32_t _serverElapsedFrame = 0;
        inline static std::int32_t _frame = 0;
        inline static std::int32_t _stuckFrames = 0;
        inline static bool _stuckDirection = false;
        static OpenTK::Mathematics::Vector3 _lastPosition;

        inline static float _aimDeltaX = 0.0F;
        inline static float _aimDeltaY = 0.0F;
        inline static std::int32_t _framesOnTarget = 0;

        inline static std::int32_t _offlineSlot = -1;
        inline static std::vector<bool> _wasDown{};
        inline static std::int32_t _releaseFrames = 0;
    };
}
