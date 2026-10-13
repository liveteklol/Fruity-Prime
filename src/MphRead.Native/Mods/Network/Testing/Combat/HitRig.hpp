#pragma once

#include "Formats/Types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Entities
{
    class PlayerControls;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // A scripted pair of clients that stresses hit registration: a shooter
    // and a moving target -- on jump pads, at long range, or a duel.
    class HitRig final
    {
    public:
        HitRig() = delete;

        enum class RigMode
        {
            Off,
            Jump,
            Sniper,
            Duel,
            Volley,
            Dialanche,
            All,
            // TEST WELLS / TEST WELLS STILL / TEST LANES: both players held in
            // place by glass, both shooting, every weapon in turn; Lanes also
            // strafes. What the hit location log (-hitlog) is measured with.
            Wells,
            Lanes,
            // A third player standing off to the side, never shooting and
            // never shot: what an observer sees of the two cells' duel.
            Observe,
            // TEST STRAFE: up to four pairs in one match, each in its own
            // walled corridor (slot / 2), both players shooting and
            // strafing side to side non-stop, nothing holding them in place:
            // the knockback is real. What the rewind log (-hitlog) is
            // measured with.
            Strafe
        };

        [[nodiscard]] static ::MphRead::BeamType VolleyWeapon() noexcept { return _volleyWeapon; }
        [[nodiscard]] static RigMode Mode() noexcept { return _mode; }
        [[nodiscard]] static bool Active() noexcept { return _mode != RigMode::Off; }
        [[nodiscard]] static bool Configure(const std::optional<std::string>& value);
        // -rigspeed F: the rig's own player strafes F times faster (cap and
        // traction), 1 to 3. The position is the owner's, so nobody else needs it.
        [[nodiscard]] static bool ConfigureSpeed(const std::optional<std::string>& value);
        // -rigstrafe N: frames each way before the strafe turns round (8 to 240).
        [[nodiscard]] static bool ConfigureStrafePeriod(const std::optional<std::string>& value);
        // -rigrange N: the two players of a strafe pair stand N units apart (8 to 40).
        [[nodiscard]] static bool ConfigureRange(const std::optional<std::string>& value);
        [[nodiscard]] static float MoveScale() noexcept { return _moveScale; }
        [[nodiscard]] static std::int32_t StrafePeriod() noexcept { return _strafePeriod; }

        [[nodiscard]] static float AimDeltaX() noexcept { return _aimDeltaX; }
        [[nodiscard]] static float AimDeltaY() noexcept { return _aimDeltaY; }
        [[nodiscard]] static std::int64_t Triggers() noexcept { return _triggers; }
        [[nodiscard]] static std::int64_t FramesOnTarget() noexcept { return _framesOnTarget; }
        [[nodiscard]] static std::int64_t FramesAirborne() noexcept { return _framesAirborne; }
        [[nodiscard]] static double RangeSum() noexcept { return _rangeSum; }
        [[nodiscard]] static std::int64_t RangeSamples() noexcept { return _rangeSamples; }
        [[nodiscard]] static float WorstVerticalSpeed() noexcept { return _worstVerticalSpeed; }
        [[nodiscard]] static double VerticalSpeedSum() noexcept { return _verticalSpeedSum; }
        [[nodiscard]] static std::int64_t VerticalSpeedSamples() noexcept { return _verticalSpeedSamples; }

        static void Reset();
        static void SetScene(Scene* scene) noexcept { _scene = scene; }
        [[nodiscard]] static bool IsSniper();
        static void Drive(Entities::PlayerEntity& player);
        [[nodiscard]] static std::string Describe();

    private:
        static constexpr float CloseRange = 9.0F;
        static constexpr float LongRange = 34.0F;
        static constexpr float VolleyRange = 16.0F;
        static constexpr float HeadAimHeight = 0.95F;
        // Mid-torso: well clear of the 0.80 line where a headshot starts.
        static constexpr float ChestAimHeight = 0.30F;
        // TEST WELLS / TEST LANES: the cells' centres, even slots on the left.
        static constexpr float CellX = 6.0F;
        static constexpr float TurnRate = 6.0F;
        static constexpr float FiringCone = 2.5F;
        // -hitrig all: the shooter holds each weapon this long, then the next.
        static constexpr std::int32_t CycleFrames = 20 * 60;
        // TEST STRAFE: corridor k runs along Z around -22.5 + 15k, the two
        // players of a pair 12 apart on X, strafing within StrafeReach of it.
        static constexpr float CorridorSpacing = 15.0F;
        static constexpr float FirstCorridorZ = -22.5F;
        static constexpr float StrafeReach = 4.5F;

        static void DriveRunner(Entities::PlayerEntity& player, Entities::PlayerControls& c, Entities::PlayerEntity* other);
        static void DriveDialanche(Entities::PlayerEntity& player, Entities::PlayerControls& c, Entities::PlayerEntity* other);
        static void DrivePadRider(Entities::PlayerEntity& player, Entities::PlayerControls& c, Entities::PlayerEntity* other);
        static void DriveWells(Entities::PlayerEntity& player, Entities::PlayerControls& c, Entities::PlayerEntity* other);
        static void DriveStrafe(Entities::PlayerEntity& player, Entities::PlayerControls& c);
        // The other player of this slot's pair (slot ^ 1), if it is in play.
        [[nodiscard]] static Entities::PlayerEntity* PairOpponent(Entities::PlayerEntity& self);
        [[nodiscard]] static float CorridorZ(std::int32_t slot) noexcept;
        // The player in the other cell (never an observer standing nearer).
        [[nodiscard]] static Entities::PlayerEntity* CellOpponent(Entities::PlayerEntity& self);
        [[nodiscard]] static ::MphRead::BeamType CycleWeapon() noexcept;
        static void DriveSniper(Entities::PlayerEntity& player, Entities::PlayerControls& c, Entities::PlayerEntity* other);
        static void HoldRange(Entities::PlayerEntity& player, Entities::PlayerControls& c, float range, float want);
        [[nodiscard]] static bool AimAt(Entities::PlayerEntity& player, Entities::PlayerEntity* target, float headHeight);
        [[nodiscard]] static Entities::PlayerEntity* Opponent(Entities::PlayerEntity& self);
        static void Square(Entities::PlayerControls& c, std::int32_t framesPerSide);
        static void ClearControls(Entities::PlayerControls& c);
        static void FinishControls(Entities::PlayerEntity& player, Entities::PlayerControls& c);

        inline static ::MphRead::BeamType _volleyWeapon = ::MphRead::BeamType::Missile;
        inline static RigMode _mode = RigMode::Off;
        inline static std::int32_t _frame = 0;
        inline static std::int32_t _stuckFrames = 0;
        inline static bool _stuckDirection = false;
        inline static OpenTK::Mathematics::Vector3 _lastPosition{};
        inline static float _aimDeltaX = 0;
        inline static float _aimDeltaY = 0;
        inline static std::int64_t _triggers = 0;
        inline static std::int64_t _framesOnTarget = 0;
        inline static std::int64_t _framesAirborne = 0;
        inline static double _rangeSum = 0;
        inline static std::int64_t _rangeSamples = 0;
        inline static float _worstVerticalSpeed = 0;
        inline static double _verticalSpeedSum = 0;
        inline static std::int64_t _verticalSpeedSamples = 0;
        inline static std::vector<bool> _wasDown{};
        inline static std::int64_t _padLaunches = 0;
        inline static std::int64_t _placements = 0;
        // -hitrig wells:magmaul -- one weapon for the whole run instead of the cycle.
        inline static bool _hasFixedWeapon = false;
        inline static ::MphRead::BeamType _fixedWeapon = ::MphRead::BeamType::PowerBeam;
        inline static std::int64_t _chargedReleases = 0;
        inline static float _moveScale = 1.0F;
        inline static float _strafeHalfRange = CellX;
        inline static std::int32_t _strafePeriod = 24;
        inline static std::int64_t _turns = 0;
        inline static float _fastest = 0;
        inline static std::int32_t _strafeSign = 1;
        inline static std::int32_t _strafeHeld = 0;
        inline static float _strafeFrom = 0;
        // -hitrig strafe:missilevolt -- the two weapons the rewind was seen
        // with, in turn, charged and uncharged.
        inline static bool _knockbackPair = false;
        // -hitrig strafe:affinity -- every player fires its own hunter's
        // affinity weapon (its own row of the table), charged one phase in two.
        inline static bool _affinity = false;
        inline static float _highest = 0;
        inline static bool _wasAirborne = false;
        inline static Scene* _scene = nullptr;
    };

    // HitRig.RigMode.ToString().
    [[nodiscard]] std::string ToString(HitRig::RigMode value);
}
