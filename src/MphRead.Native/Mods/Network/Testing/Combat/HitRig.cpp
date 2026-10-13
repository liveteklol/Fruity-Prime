#include "Mods/Network/Testing/Combat/HitRig.hpp"

#include "Mods/Network/Gameplay/Combat/HitLocation.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Metadata/Weapons.hpp"
#include "Entities/JumpPadEntity.hpp"
#include "Scene.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::PlayerControls;
    using Entities::PlayerEntity;

    std::string ToString(HitRig::RigMode value)
    {
        switch (value)
        {
        case HitRig::RigMode::Off: return "Off";
        case HitRig::RigMode::Jump: return "Jump";
        case HitRig::RigMode::Sniper: return "Sniper";
        case HitRig::RigMode::Duel: return "Duel";
        case HitRig::RigMode::Volley: return "Volley";
        case HitRig::RigMode::Dialanche: return "Dialanche";
        case HitRig::RigMode::All: return "All";
        case HitRig::RigMode::Wells: return "Wells";
        case HitRig::RigMode::Lanes: return "Lanes";
        case HitRig::RigMode::Observe: return "Observe";
        case HitRig::RigMode::Strafe: return "Strafe";
        }
        return std::to_string(static_cast<std::int32_t>(value));
    }

    bool HitRig::Configure(const std::optional<std::string>& value)
    {
        if (!value.has_value())
        {
            return false;
        }
        std::string key = Runtime::ToLowerInvariant(Runtime::StringTrim(*value));
        // wells:magmaul / lanes:judicator: the cell rig with one weapon.
        if (const std::size_t colon = key.find(':'); colon != std::string::npos)
        {
            const std::string weapon = key.substr(colon + 1);
            key = key.substr(0, colon);
            if (key == "strafe" && (weapon == "missilevolt" || weapon == "knockback"))
            {
                _knockbackPair = true;
            }
            else if (key == "strafe" && weapon == "affinity")
            {
                _affinity = true;
            }
            else if (!Configure(weapon) || _mode != RigMode::Volley
                || (key != "wells" && key != "lanes" && key != "strafe"))
            {
                return false;
            }
            else
            {
                _hasFixedWeapon = true;
                _fixedWeapon = _volleyWeapon;
            }
        }
        const auto volley = [](::MphRead::BeamType weapon)
        {
            _mode = RigMode::Volley;
            _volleyWeapon = weapon;
            return true;
        };
        if (key == "jump" || key == "jumppad")
        {
            _mode = RigMode::Jump;
            return true;
        }
        if (key == "sniper" || key == "long")
        {
            _mode = RigMode::Sniper;
            return true;
        }
        if (key == "duel" || key == "trade")
        {
            _mode = RigMode::Duel;
            return true;
        }
        if (key == "missile") return volley(::MphRead::BeamType::Missile);
        if (key == "magmaul") return volley(::MphRead::BeamType::Magmaul);
        if (key == "judicator") return volley(::MphRead::BeamType::Judicator);
        if (key == "battlehammer") return volley(::MphRead::BeamType::Battlehammer);
        if (key == "shockcoil") return volley(::MphRead::BeamType::ShockCoil);
        if (key == "voltdriver") return volley(::MphRead::BeamType::VoltDriver);
        if (key == "powerbeam") return volley(::MphRead::BeamType::PowerBeam);
        if (key == "imperialist") return volley(::MphRead::BeamType::Imperialist);
        if (key == "omega" || key == "omegacannon") return volley(::MphRead::BeamType::OmegaCannon);
        if (key == "dialanche") { _mode = RigMode::Dialanche; return true; }
        // Every weapon in turn against a target that never leaves the jump
        // pads: the runner walks back onto the nearest pad each time it lands.
        if (key == "all" || key == "pads") { _mode = RigMode::All; return true; }
        if (key == "wells") { _mode = RigMode::Wells; return true; }
        if (key == "lanes") { _mode = RigMode::Lanes; return true; }
        if (key == "observe" || key == "observer") { _mode = RigMode::Observe; return true; }
        if (key == "strafe" || key == "rewind") { _mode = RigMode::Strafe; return true; }
        return false;
    }

    bool HitRig::ConfigureSpeed(const std::optional<std::string>& value)
    {
        if (!value.has_value())
        {
            return false;
        }
        try
        {
            const float scale = std::stof(*value);
            if (!(scale >= 1.0F && scale <= 3.0F))
            {
                return false;
            }
            _moveScale = scale;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool HitRig::ConfigureRange(const std::optional<std::string>& value)
    {
        if (!value.has_value())
        {
            return false;
        }
        try
        {
            const float range = std::stof(*value);
            if (!(range >= 8.0F && range <= 40.0F))
            {
                return false;
            }
            _strafeHalfRange = range / 2.0F;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool HitRig::ConfigureStrafePeriod(const std::optional<std::string>& value)
    {
        if (!value.has_value())
        {
            return false;
        }
        try
        {
            const std::int32_t frames = std::stoi(*value);
            if (frames < 8 || frames > 240)
            {
                return false;
            }
            _strafePeriod = frames;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void HitRig::Reset()
    {
        _frame = 0;
        _stuckFrames = 0;
        _lastPosition = OpenTK::Mathematics::Vector3::Zero;
        _aimDeltaX = 0;
        _aimDeltaY = 0;
        _triggers = 0;
        _framesOnTarget = 0;
        _framesAirborne = 0;
        _rangeSum = 0;
        _rangeSamples = 0;
        _worstVerticalSpeed = 0;
        _verticalSpeedSum = 0;
        _verticalSpeedSamples = 0;
        _padLaunches = 0;
        _wasAirborne = false;
        _placements = 0;
        _highest = 0;
        _turns = 0;
        _fastest = 0;
    }

    bool HitRig::IsSniper()
    {
        return _mode == RigMode::Duel || std::max(NetSession::LocalSlot(), 0) % 2 == 0;
    }

    void HitRig::Drive(PlayerEntity& player)
    {
        Runtime::IncrementInPlace(_frame);
        HitLocation::Watch();
        PlayerControls& c = player.Controls();
        ClearControls(c);
        if (player.Health() == 0)
        {
            c.Shoot().SetIsDown(true);
            _aimDeltaX = 0;
            _aimDeltaY = -TurnRate;
            FinishControls(player, c);
            return;
        }
        PlayerEntity* other = Opponent(player);
        if (_mode == RigMode::Dialanche)
        {
            DriveDialanche(player, c, other);
        }
        else if (_mode == RigMode::Wells || _mode == RigMode::Lanes)
        {
            DriveWells(player, c, CellOpponent(player));
        }
        else if (_mode == RigMode::Strafe)
        {
            DriveStrafe(player, c);
        }
        else if (_mode == RigMode::Observe)
        {
            _aimDeltaX = _aimDeltaY = 0;
            const OpenTK::Mathematics::Vector3 at = player.Position;
            if (std::abs(at.X) > 1.0F || std::abs(at.Z - 9.0F) > 1.0F)
            {
                player.ModPlaceAt(OpenTK::Mathematics::Vector3(0.0F, 0.6F, 9.0F));
                Runtime::IncrementInPlace(_placements);
            }
        }
        else if (IsSniper())
        {
            DriveSniper(player, c, other);
        }
        else if (_mode == RigMode::All)
        {
            DrivePadRider(player, c, other);
        }
        else
        {
            DriveRunner(player, c, other);
        }
        FinishControls(player, c);
    }

    void HitRig::DriveDialanche(PlayerEntity& player, PlayerControls& c, PlayerEntity* other)
    {
        _aimDeltaX = _aimDeltaY = 0;
        // A real Spire intent stream approaches a human target that settles
        // after a short walk (also exercises the client's movement report).
        // No teleport, forced form, damage injection, or authority hook.
        if (player.Hunter() != ::MphRead::Hunter::Spire)
        {
            c.MoveUp().SetIsDown(_frame < 60);
            return;
        }
        if (other == nullptr) return;
        const bool onTarget = AimAt(player, other, 0);
        const float range = OpenTK::Mathematics::Length(static_cast<OpenTK::Mathematics::Vector3>(player.Position)
            - static_cast<OpenTK::Mathematics::Vector3>(other->Position));
        if (!player.IsAltForm())
        {
            c.MoveUp().SetIsDown(onTarget && range > 1.1F);
            c.Morph().SetIsDown(range < 1.5F && !player.IsMorphing() && !player.IsUnmorphing() && _frame % 20 == 0);
            return;
        }
        c.RollUp().SetIsDown(onTarget && range > 1.1F);
        if (onTarget && range < 2.5F && !player.IsMorphing() && _frame % 48 == 0)
        {
            c.AltAttack().SetIsDown(true);
            ++_triggers;
        }
        _framesOnTarget += onTarget ? 1 : 0;
        _rangeSum += range;
        ++_rangeSamples;
    }

    void HitRig::DriveRunner(PlayerEntity& player, PlayerControls& c, PlayerEntity* other)
    {
        static_cast<void>(AimAt(player, other, 0));
        const bool airborne = !::MphRead::TestFlag(player.Flags1(), Entities::PlayerFlags1::Standing);
        if (airborne)
        {
            Runtime::IncrementInPlace(_framesAirborne);
        }
        const float rise = std::abs(player.Speed().Y);
        _verticalSpeedSum += rise;
        Runtime::IncrementInPlace(_verticalSpeedSamples);
        if (rise > _worstVerticalSpeed)
        {
            _worstVerticalSpeed = rise;
        }
        c.Jump().SetIsDown(_mode == RigMode::Jump ? _frame % 24 < 3 : _frame % 90 < 3);
        Square(c, _mode == RigMode::Jump ? 50 : 80);
    }

    void HitRig::DriveWells(PlayerEntity& player, PlayerControls& c, PlayerEntity* other)
    {
        // A client picks its own spawn, at least ten units from anybody it
        // can see -- and at the very first spawn it sees nobody yet, so two
        // players can land in the same well. Each slot has its cell; a player
        // found outside it is put back (the authority takes the reported
        // position, as it does every frame).
        const float cellX = NetSession::LocalSlot() % 2 == 0 ? -CellX : CellX;
        const OpenTK::Mathematics::Vector3 at = player.Position;
        if (std::abs(at.X - cellX) > 2.0F)
        {
            player.ModPlaceAt(OpenTK::Mathematics::Vector3(cellX, 0.6F, 0.0F));
            Runtime::IncrementInPlace(_placements);
            return;
        }
        _highest = std::max(_highest, at.Y);
        const bool airborne = !::MphRead::TestFlag(player.Flags1(), Entities::PlayerFlags1::Standing);
        if (airborne)
        {
            Runtime::IncrementInPlace(_framesAirborne);
        }
        const float rise = std::abs(player.Speed().Y);
        _verticalSpeedSum += rise;
        Runtime::IncrementInPlace(_verticalSpeedSamples);
        _worstVerticalSpeed = std::max(_worstVerticalSpeed, rise);
        // The glass and the pad do the moving; in a lane the rig strafes.
        if (_mode == RigMode::Lanes)
        {
            const bool left = _frame / 40 % 2 == 0;
            c.MoveLeft().SetIsDown(left);
            c.MoveRight().SetIsDown(!left);
        }
        const ::MphRead::BeamType weapon = CycleWeapon();
        if (player.CurrentWeapon() != weapon)
        {
            player.ModArmWeapon(weapon);
        }
        player.ModSetAmmo(std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max());
        // Head and chest in turn, five seconds each: the two bands whose
        // boundary the location measurement has to agree on.
        const bool charging = weapon == ::MphRead::BeamType::Magmaul && _frame / 100 % 2 == 0;
        // A charged Magmaul round falls about a unit and a half over the 12
        // between the cells: aimed over the head, it lands on the body.
        const float height = charging ? 2.6F : _frame / 300 % 2 == 0 ? HeadAimHeight : ChestAimHeight;
        const bool onTarget = AimAt(player, other, height);
        if (onTarget)
        {
            Runtime::IncrementInPlace(_framesOnTarget);
        }
        const std::int32_t tap = Runtime::RequireReference(
            (*::MphRead::Weapons::Current)[static_cast<std::size_t>(weapon)]).ShotCooldown * 2 + 3;
        // The Magmaul burns only when charged: every other 100 frames it is
        // held 80 and released, so the burn is in the measurement too.
        if (charging)
        {
            const std::int32_t phase = static_cast<std::int32_t>(_frame % 100);
            c.Shoot().SetIsDown(phase < 80 && (onTarget || phase > 0));
            if (phase == 80)
            {
                Runtime::IncrementInPlace(_chargedReleases);
            }
            return;
        }
        c.Shoot().SetIsDown(onTarget && _frame % tap < 3);
        if (c.Shoot().IsDown() && _frame % tap == 0)
        {
            Runtime::IncrementInPlace(_triggers);
        }
    }

    float HitRig::CorridorZ(std::int32_t slot) noexcept
    {
        return FirstCorridorZ + CorridorSpacing * static_cast<float>(std::clamp(slot, 0, 7) / 2);
    }

    PlayerEntity* HitRig::PairOpponent(PlayerEntity& self)
    {
        const std::int32_t want = NetSession::LocalSlot() ^ 1;
        const auto& players = PlayerEntity::Players();
        if (want < 0 || static_cast<std::size_t>(want) >= players.size())
        {
            return nullptr;
        }
        PlayerEntity& other = Runtime::RequireReference(players[static_cast<std::size_t>(want)]);
        if (&other == &self || !::MphRead::TestFlag(other.LoadFlags(), Entities::LoadFlags::Active)
            || !::MphRead::TestFlag(other.LoadFlags(), Entities::LoadFlags::Spawned) || other.Health() == 0)
        {
            return nullptr;
        }
        return &other;
    }

    void HitRig::DriveStrafe(PlayerEntity& player, PlayerControls& c)
    {
        // Each pair has its corridor (slot / 2) and each player its side
        // (slot % 2). A client picks its own spawn before it sees anybody, so
        // one found in another corridor, or across the middle, is put back.
        // Nothing else moves a player: a knockback is walked back from.
        const std::int32_t slot = std::max(NetSession::LocalSlot(), 0);
        const float cellX = slot % 2 == 0 ? -_strafeHalfRange : _strafeHalfRange;
        const float cellZ = CorridorZ(slot);
        const OpenTK::Mathematics::Vector3 at = player.Position;
        if (std::abs(at.Z - cellZ) > CorridorSpacing / 2.0F - 1.0F || at.X * cellX < 1.0F)
        {
            player.ModPlaceAt(OpenTK::Mathematics::Vector3(cellX, 0.6F, cellZ));
            Runtime::IncrementInPlace(_placements);
            HitLocation::Placed(player);
            return;
        }
        const OpenTK::Mathematics::Vector3 speed = player.Speed();
        const float flat = std::sqrt(speed.X * speed.X + speed.Z * speed.Z);
        _fastest = std::max(_fastest, flat);
        _rangeSum += flat;
        Runtime::IncrementInPlace(_rangeSamples);
        PlayerEntity* other = PairOpponent(player);

        // Side to side along Z, turning round every StrafePeriod frames or
        // at the end of the reach. Which key moves which way depends on the
        // facing, so it is learned: a key held for a while that moved the
        // player the wrong way swaps the two.
        std::int32_t want = _frame / _strafePeriod % 2 == 0 ? 1 : -1;
        if (at.Z > cellZ + StrafeReach)
        {
            want = -1;
        }
        else if (at.Z < cellZ - StrafeReach)
        {
            want = 1;
        }
        static std::int32_t previousWant = 0;
        if (want != previousWant)
        {
            Runtime::IncrementInPlace(_turns);
            previousWant = want;
            _strafeHeld = 0;
            _strafeFrom = at.Z;
        }
        else if (++_strafeHeld == 12 && (at.Z - _strafeFrom) * static_cast<float>(want) < -0.2F)
        {
            _strafeSign = -_strafeSign;
        }
        const bool right = want * _strafeSign > 0;
        c.MoveRight().SetIsDown(right);
        c.MoveLeft().SetIsDown(!right);
        // Range: back to 12 units from the other side after a knock.
        const float fromMiddle = std::abs(at.X);
        c.MoveUp().SetIsDown(fromMiddle > _strafeHalfRange + 0.8F);
        c.MoveDown().SetIsDown(fromMiddle < _strafeHalfRange - 0.8F);

        ::MphRead::BeamType weapon = CycleWeapon();
        if (_affinity)
        {
            weapon = ::MphRead::Weapons::GetAffinityBeam(player.Hunter());
        }
        else if (_knockbackPair)
        {
            weapon = (_frame / CycleFrames + slot / 2) % 2 == 0 ? ::MphRead::BeamType::Missile
                : ::MphRead::BeamType::VoltDriver;
        }
        else if (!_hasFixedWeapon)
        {
            // Every pair on another weapon at any moment: four in parallel.
            static constexpr std::int32_t Weapons = 9;
            static constexpr ::MphRead::BeamType Cycle[] = {
                ::MphRead::BeamType::PowerBeam, ::MphRead::BeamType::VoltDriver,
                ::MphRead::BeamType::Missile, ::MphRead::BeamType::Battlehammer,
                ::MphRead::BeamType::Imperialist, ::MphRead::BeamType::Judicator,
                ::MphRead::BeamType::Magmaul, ::MphRead::BeamType::ShockCoil,
                ::MphRead::BeamType::OmegaCannon,
            };
            weapon = Cycle[static_cast<std::size_t>((_frame / CycleFrames + 2 * (slot / 2)) % Weapons)];
        }
        if (player.CurrentWeapon() != weapon)
        {
            player.ModArmWeapon(weapon);
        }
        player.ModSetAmmo(std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max());
        const bool onTarget = AimAt(player, other, _frame / 300 % 2 == 0 ? HeadAimHeight : ChestAimHeight);
        if (onTarget)
        {
            Runtime::IncrementInPlace(_framesOnTarget);
        }
        const auto& info = Runtime::RequireReference((*::MphRead::Weapons::Current)[static_cast<std::size_t>(weapon)]);
        // The missile and the Volt Driver are fired charged one phase in two
        // (their charged shots are the ones that knock hardest).
        const bool chargeable = _affinity
            ? ::MphRead::TestFlag(info.Flags, ::MphRead::WeaponFlags::CanCharge)
            : weapon == ::MphRead::BeamType::Missile || weapon == ::MphRead::BeamType::VoltDriver
                || weapon == ::MphRead::BeamType::Magmaul;
        const std::int32_t hold = static_cast<std::int32_t>(info.FullCharge) * 2 + 8;
        const std::int32_t phaseLength = std::max(hold + 20, 100);
        if (chargeable && _frame / phaseLength % 2 == 1)
        {
            const std::int32_t phase = static_cast<std::int32_t>(_frame % phaseLength);
            c.Shoot().SetIsDown(phase < hold && (onTarget || phase > 0));
            if (phase == hold)
            {
                Runtime::IncrementInPlace(_chargedReleases);
            }
            return;
        }
        const std::int32_t tap = static_cast<std::int32_t>(info.ShotCooldown) * 2 + 3;
        c.Shoot().SetIsDown(onTarget && _frame % tap < 3);
        if (c.Shoot().IsDown() && _frame % tap == 0)
        {
            Runtime::IncrementInPlace(_triggers);
        }
    }

    ::MphRead::BeamType HitRig::CycleWeapon() noexcept
    {
        if (_hasFixedWeapon)
        {
            return _fixedWeapon;
        }
        static constexpr ::MphRead::BeamType Cycle[] = {
            ::MphRead::BeamType::PowerBeam, ::MphRead::BeamType::VoltDriver,
            ::MphRead::BeamType::Missile, ::MphRead::BeamType::Battlehammer,
            ::MphRead::BeamType::Imperialist, ::MphRead::BeamType::Judicator,
            ::MphRead::BeamType::Magmaul, ::MphRead::BeamType::ShockCoil,
            ::MphRead::BeamType::OmegaCannon,
        };
        return Cycle[static_cast<std::size_t>(_frame / CycleFrames) % std::size(Cycle)];
    }

    namespace
    {
        OpenTK::Mathematics::Vector3 PadCenter(const ::MphRead::CollisionVolume& volume)
        {
            using OpenTK::Mathematics::Scale;
            using OpenTK::Mathematics::Vector3;
            switch (volume.Type)
            {
            case ::MphRead::VolumeType::Box:
                return volume.BoxPosition + Scale(Scale(volume.BoxVector1, volume.BoxDot1)
                    + Scale(volume.BoxVector2, volume.BoxDot2) + Scale(volume.BoxVector3, volume.BoxDot3), 0.5F);
            case ::MphRead::VolumeType::Cylinder:
                return volume.CylinderPosition + Scale(volume.CylinderVector, volume.CylinderDot / 2.0F);
            case ::MphRead::VolumeType::Sphere:
                return volume.SpherePosition;
            default:
                return Vector3::Zero;
            }
        }
    }

    void HitRig::DrivePadRider(PlayerEntity& player, PlayerControls& c, PlayerEntity* other)
    {
        using OpenTK::Mathematics::Vector3;
        const bool airborne = !::MphRead::TestFlag(player.Flags1(), Entities::PlayerFlags1::Standing);
        const float rise = std::abs(player.Speed().Y);
        _verticalSpeedSum += rise;
        Runtime::IncrementInPlace(_verticalSpeedSamples);
        _worstVerticalSpeed = std::max(_worstVerticalSpeed, rise);
        if (airborne)
        {
            Runtime::IncrementInPlace(_framesAirborne);
            // A rise this steep is a pad, not a jump: count the launches so a
            // run can tell a rider that kept riding from one stuck in a corner.
            if (!_wasAirborne && player.Speed().Y > 0.5F)
            {
                Runtime::IncrementInPlace(_padLaunches);
            }
            _wasAirborne = true;
            // In the air the pad does the steering, so the rider shoots back
            // with the same weapon of the cycle: that is what puts two players
            // killing each other inside one round trip, the case the kill
            // arbitration exists for.
            const ::MphRead::BeamType weapon = CycleWeapon();
            if (player.CurrentWeapon() != weapon)
            {
                player.ModArmWeapon(weapon);
            }
            player.ModSetAmmo(std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max());
            const bool onTarget = AimAt(player, other, HeadAimHeight);
            const std::int32_t tap = Runtime::RequireReference(
                (*::MphRead::Weapons::Current)[static_cast<std::size_t>(weapon)]).ShotCooldown * 2 + 3;
            c.Shoot().SetIsDown(onTarget && _frame % tap < 3);
            if (c.Shoot().IsDown() && _frame % tap == 0)
            {
                Runtime::IncrementInPlace(_triggers);
            }
            return;
        }
        _wasAirborne = false;
        Scene* scene = _scene;
        if (scene == nullptr)
        {
            Square(c, 50);
            return;
        }
        const Vector3 position = player.Position;
        Vector3 best = Vector3::Zero;
        float bestDistance = std::numeric_limits<float>::max();
        auto entities = scene->Entities().GetEnumerator();
        while (entities.MoveNext())
        {
            auto* pad = dynamic_cast<Entities::JumpPadEntity*>(entities.Current().get());
            if (pad == nullptr || !pad->Active)
            {
                continue;
            }
            const Vector3 center = PadCenter(pad->ModVolume());
            const float dx = center.X - position.X;
            const float dz = center.Z - position.Z;
            const float distance = dx * dx + dz * dz;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = center;
            }
        }
        if (bestDistance == std::numeric_limits<float>::max())
        {
            // No pads in this room: fall back to the jump runner.
            c.Jump().SetIsDown(_frame % 24 < 3);
            Square(c, 50);
            return;
        }
        // Face the pad at eye height and walk onto it. The pad does the rest.
        const auto [turnX, turnY] = player.ModAimDeltaTowards(
            Vector3(best.X, position.Y + 0.6F, best.Z));
        if (!std::isfinite(turnX) || !std::isfinite(turnY))
        {
            return;
        }
        _aimDeltaX = std::clamp(turnX, -TurnRate, TurnRate);
        _aimDeltaY = std::clamp(turnY, -TurnRate, TurnRate);
        c.MoveUp().SetIsDown(std::abs(turnX) < 45.0F);
    }

    void HitRig::DriveSniper(PlayerEntity& player, PlayerControls& c, PlayerEntity* other)
    {
        if (_mode == RigMode::Volley || _mode == RigMode::All)
        {
            const ::MphRead::BeamType weapon = _mode == RigMode::All ? CycleWeapon() : _volleyWeapon;
            if (player.CurrentWeapon() != weapon)
            {
                player.ModArmWeapon(weapon);
            }
        }
        else if (player.CurrentWeapon() != ::MphRead::BeamType::Imperialist)
        {
            player.ModArmZoomWeapon();
        }
        player.ModSetAmmo(std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max());
        const bool volley = _mode == RigMode::Volley || _mode == RigMode::All;
        const ::MphRead::BeamType weapon = _mode == RigMode::All ? CycleWeapon() : _volleyWeapon;
        if (!volley && player.ModCanZoom()
            && !player.EquipInfo()->Zoomed && _frame % 8 == 0)
        {
            c.Zoom().SetIsDown(true);
        }
        const bool onTarget = AimAt(player, other, HeadAimHeight);
        if (onTarget)
        {
            Runtime::IncrementInPlace(_framesOnTarget);
        }
        if (other == nullptr)
        {
            Square(c, 60);
            return;
        }
        const OpenTK::Mathematics::Vector3 otherPosition = other->Position;
        const OpenTK::Mathematics::Vector3 position = player.Position;
        const float range = OpenTK::Mathematics::Length(otherPosition - position);
        _rangeSum += range;
        Runtime::IncrementInPlace(_rangeSamples);
        HoldRange(player, c, range, _mode == RigMode::Sniper ? LongRange
            : volley && weapon != ::MphRead::BeamType::ShockCoil ? VolleyRange : CloseRange);
        const std::int32_t tap = volley
            ? Runtime::RequireReference((*::MphRead::Weapons::Current)[static_cast<std::size_t>(weapon)]).ShotCooldown * 2 + 3
            : 63;
        const std::int32_t clock = _mode == RigMode::Duel && NetSession::LastSnapshotFrame() != 0
            ? std::bit_cast<std::int32_t>(NetSession::LastSnapshotFrame())
            : _frame;
        c.Shoot().SetIsDown(onTarget && clock % tap < 3);
        if (c.Shoot().IsDown() && clock % tap == 0)
        {
            Runtime::IncrementInPlace(_triggers);
        }
    }

    void HitRig::HoldRange(PlayerEntity& player, PlayerControls& c, float range, float want)
    {
        const OpenTK::Mathematics::Vector3 position = player.Position;
        const float moved = OpenTK::Mathematics::Length(position - _lastPosition);
        _lastPosition = position;
        _stuckFrames = moved < 0.02F ? _stuckFrames + 1 : 0;
        const bool stuck = _stuckFrames > 20;
        if (stuck && _stuckFrames > 90)
        {
            _stuckFrames = 0;
            _stuckDirection = !_stuckDirection;
        }
        if (stuck)
        {
            c.MoveLeft().SetIsDown(_stuckDirection);
            c.MoveRight().SetIsDown(!_stuckDirection);
            c.Jump().SetIsDown(_stuckFrames % 30 < 3);
            return;
        }
        constexpr float slack = 2.5F;
        c.MoveUp().SetIsDown(range > want + slack);
        c.MoveDown().SetIsDown(range < want - slack);
        if (!c.MoveUp().IsDown() && !c.MoveDown().IsDown())
        {
            c.MoveLeft().SetIsDown(_frame / 70 % 2 == 0);
            c.MoveRight().SetIsDown(_frame / 70 % 2 == 1);
        }
    }

    bool HitRig::AimAt(PlayerEntity& player, PlayerEntity* target, float headHeight)
    {
        _aimDeltaX = 0;
        _aimDeltaY = 0;
        if (target == nullptr)
        {
            return false;
        }
        const OpenTK::Mathematics::Vector3 targetPosition = target->Position;
        const OpenTK::Mathematics::Vector3 at = headHeight > 0
            ? OpenTK::Mathematics::Vector3(targetPosition.X, targetPosition.Y + headHeight, targetPosition.Z)
            : target->ModAimTarget();
        const auto [turnX, turnY] = player.ModAimDeltaTowards(at);
        if (!std::isfinite(turnX) || !std::isfinite(turnY))
        {
            return false;
        }
        _aimDeltaX = std::clamp(turnX, -TurnRate, TurnRate);
        _aimDeltaY = std::clamp(turnY, -TurnRate, TurnRate);
        return std::abs(turnX) < FiringCone && std::abs(turnY) < FiringCone;
    }

    PlayerEntity* HitRig::CellOpponent(PlayerEntity& self)
    {
        const float mine = static_cast<OpenTK::Mathematics::Vector3>(self.Position).X;
        for (const std::shared_ptr<PlayerEntity>& otherPtr : PlayerEntity::Players())
        {
            PlayerEntity& other = Runtime::RequireReference(otherPtr);
            const float x = static_cast<OpenTK::Mathematics::Vector3>(other.Position).X;
            if (&other != &self && ::MphRead::TestFlag(other.LoadFlags(), Entities::LoadFlags::Active)
                && ::MphRead::TestFlag(other.LoadFlags(), Entities::LoadFlags::Spawned) && other.Health() > 0
                && std::abs(x + mine) < 2.5F && std::abs(x) > CellX - 2.5F)
            {
                return &other;
            }
        }
        return nullptr;
    }

    PlayerEntity* HitRig::Opponent(PlayerEntity& self)
    {
        PlayerEntity* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();
        for (const std::shared_ptr<PlayerEntity>& otherPtr : PlayerEntity::Players())
        {
            PlayerEntity& other = Runtime::RequireReference(otherPtr);
            if (&other == &self || !::MphRead::TestFlag(other.LoadFlags(), Entities::LoadFlags::Active)
                || !::MphRead::TestFlag(other.LoadFlags(), Entities::LoadFlags::Spawned) || other.Health() == 0)
            {
                continue;
            }
            const OpenTK::Mathematics::Vector3 a = other.Position;
            const OpenTK::Mathematics::Vector3 b = self.Position;
            const float distance = OpenTK::Mathematics::LengthSquared(a - b);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = &other;
            }
        }
        return best;
    }

    void HitRig::Square(PlayerControls& c, std::int32_t framesPerSide)
    {
        const std::int32_t side = _frame / framesPerSide % 4;
        c.MoveUp().SetIsDown(side == 0);
        c.MoveRight().SetIsDown(side == 1);
        c.MoveDown().SetIsDown(side == 2);
        c.MoveLeft().SetIsDown(side == 3);
    }

    void HitRig::ClearControls(PlayerControls& c)
    {
        if (_wasDown.size() < c.All().size())
        {
            _wasDown.assign(c.All().size(), false);
        }
        for (std::size_t i = 0; i < c.All().size(); i++)
        {
            Entities::Keybind& bind = *c.All()[static_cast<std::int32_t>(i)];
            _wasDown[i] = bind.IsDown();
            bind.SetIsDown(false);
            bind.SetIsPressed(false);
            bind.SetIsReleased(false);
        }
    }

    void HitRig::FinishControls(PlayerEntity& player, PlayerControls& c)
    {
        bool any = false;
        for (std::size_t i = 0; i < c.All().size() && i < _wasDown.size(); i++)
        {
            Entities::Keybind& bind = *c.All()[static_cast<std::int32_t>(i)];
            bind.SetIsPressed(bind.IsDown() && !_wasDown[i]);
            bind.SetIsReleased(!bind.IsDown() && _wasDown[i]);
            any |= bind.IsDown() || bind.IsReleased();
        }
        if (any)
        {
            player.ModNoteInput();
        }
        player.ModApplyScriptAim(_aimDeltaX, _aimDeltaY);
    }

    std::string HitRig::Describe()
    {
        if (!Active())
        {
            return "hit rig: off";
        }
        if (_mode == RigMode::Dialanche)
        {
            return "hit rig: Dialanche, " + std::to_string(_triggers) + " attack press edges, "
                + std::to_string(_framesOnTarget) + " frames on target";
        }
        if (_mode == RigMode::Strafe)
        {
            const double mean = _rangeSamples > 0 ? _rangeSum / static_cast<double>(_rangeSamples) : 0;
            return "hit rig: Strafe, slot " + std::to_string(NetSession::LocalSlot()) + " (pair "
                + std::to_string(std::max(NetSession::LocalSlot(), 0) / 2) + "), range " + Runtime::ToString(_strafeHalfRange * 2.0F, "F0") + ", speed x" + Runtime::ToString(_moveScale, "F2")
                + ", turn every " + std::to_string(_strafePeriod) + " frames, " + std::to_string(_turns) + " turns, "
                + "mean |horizontal speed| " + Runtime::ToString(mean, "F3") + " units/frame, fastest "
                + Runtime::ToString(_fastest, "F3") + ", " + std::to_string(_placements) + " placement(s), "
                + std::to_string(_triggers) + " triggers, " + std::to_string(_chargedReleases) + " charged releases, "
                + std::to_string(_framesOnTarget) + " frames on target";
        }
        const std::string role = _mode == RigMode::Wells ? "well" : _mode == RigMode::Lanes ? "lane"
            : IsSniper() ? "sniper" : _mode == RigMode::All ? "pad rider" : "runner";
        if (IsSniper() && _mode != RigMode::Wells && _mode != RigMode::Lanes)
        {
            const double range = _rangeSamples > 0 ? _rangeSum / static_cast<double>(_rangeSamples) : 0;
            return "hit rig: " + ToString(_mode) + " as " + role + ", " + std::to_string(_triggers) + " triggers, "
                + std::to_string(_framesOnTarget) + " frames on target, mean range " + Runtime::ToString(range, "F1") + " units";
        }
        const double rise = _verticalSpeedSamples > 0 ? _verticalSpeedSum / static_cast<double>(_verticalSpeedSamples) : 0;
        return "hit rig: " + ToString(_mode) + " as " + role + ", " + std::to_string(_framesAirborne) + " frames airborne, "
            + "mean |vertical speed| " + Runtime::ToString(rise, "F3") + " units/frame, worst "
            + Runtime::ToString(_worstVerticalSpeed, "F3")
            + (_mode == RigMode::All ? ", " + std::to_string(_padLaunches) + " pad launches, "
                + std::to_string(_triggers) + " triggers in the air" : std::string())
            + (_mode == RigMode::Wells || _mode == RigMode::Lanes ? ", highest " + Runtime::ToString(_highest, "F2")
                + ", " + std::to_string(_placements) + " placement(s) into the cell, " + std::to_string(_triggers)
                + " triggers, " + std::to_string(_chargedReleases) + " charged releases, "
                + std::to_string(_framesOnTarget) + " frames on target" : std::string());
    }
}
