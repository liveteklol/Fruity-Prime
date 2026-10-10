#include "NetTestScript.hpp"
#include "HitRig.hpp"
#include "../../NativeRuntime/System/Enum.hpp"

#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Number.hpp"

#include "NetSession.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using ::MphRead::NativeRuntime::DecrementInPlace;
using ::MphRead::NativeRuntime::ConvertToInt32Net9;
using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::NumberFormatInfo;
using ::MphRead::NativeRuntime::NumberStyles;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::TryParseDouble;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::Length;

namespace
{
    [[nodiscard]] std::size_t CheckedIndex(std::int32_t index, std::size_t length)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= length)
        {
            throw MphRead::SceneDetail::IndexOutOfRangeException();
        }
        return static_cast<std::size_t>(index);
    }
}

namespace MphRead::Mods::Network
{
    double NetTestScript::_phaseSeconds = NetTestScript::ReadPhaseSeconds();
    std::optional<TestPhase> NetTestScript::_pinnedPhase = NetTestScript::ReadPinnedPhase();
    OpenTK::Mathematics::Vector3 NetTestScript::_lastPosition
        = OpenTK::Mathematics::Vector3::Zero;

    double NetTestScript::PhaseSeconds() noexcept
    {
        return _phaseSeconds;
    }

    void NetTestScript::SetPhaseSeconds(double value) noexcept
    {
        _phaseSeconds = value;
    }

    std::int32_t NetTestScript::PhaseCount() noexcept
    {
        return static_cast<std::int32_t>(_order.size());
    }

    bool NetTestScript::Enabled() noexcept
    {
        return _enabled;
    }

    void NetTestScript::SetEnabled(bool value) noexcept
    {
        _enabled = value;
    }

    float NetTestScript::AimDeltaX() noexcept
    {
        return _aimDeltaX;
    }

    float NetTestScript::AimDeltaY() noexcept
    {
        return _aimDeltaY;
    }

    std::int32_t NetTestScript::FramesOnTarget() noexcept
    {
        return _framesOnTarget;
    }

    double NetTestScript::ReadPhaseSeconds()
    {
        const std::optional<std::string> value
            = NativeRuntime::EnvironmentGetVariable("MPHREAD_PHASE_SECONDS");
        double parsed = 0.0;
        if (value.has_value()
            && TryParseDouble(*value, NumberStyles::Float | NumberStyles::AllowThousands,
                NumberFormatInfo::InvariantInfo(), parsed)
            && parsed > 0.0)
        {
            return parsed;
        }
        return 5.0;
    }

    std::optional<TestPhase> NetTestScript::ReadPinnedPhase()
    {
        const std::optional<std::string> value = NativeRuntime::EnvironmentGetVariable("MPHREAD_PHASE");
        if (value.has_value())
        {
            for (const TestPhase phase : _order)
            {
                if (ToString(phase) == *value)
                {
                    return phase;
                }
            }
        }
        return std::nullopt;
    }

    TestPhase NetTestScript::Phase()
    {
        if (_pinnedPhase.has_value())
        {
            return *_pinnedPhase;
        }
        const std::optional<MatchStatePacket> serverMatch = NetSession::ServerMatch();
        const double elapsed = serverMatch.has_value()
            ? ServerElapsed(serverMatch->TimeElapsed)
            : static_cast<double>(_frame) / 60.0;
        const std::int32_t quotient
            = ConvertToInt32Net9(elapsed / PhaseSeconds());
        const std::int32_t index
            = quotient % static_cast<std::int32_t>(_order.size());
        return _order[CheckedIndex(index, _order.size())];
    }

    double NetTestScript::ServerElapsed(float received) noexcept
    {
        const std::uint32_t frame = NetSession::NetFrame();
        const double carried = static_cast<double>(_serverElapsed)
            + static_cast<double>(frame - _serverElapsedFrame) / 60.0;
        // A packet re-anchors the clock forward. One a little behind it is
        // the server a one-way trip ago, not a clock running backwards; one
        // far behind is a new match.
        constexpr double NewMatchStep = 2.0;
        if (received != _serverElapsed
            && (_serverElapsed < 0.0F || received >= carried || carried - received > NewMatchStep))
        {
            _serverElapsed = received;
            _serverElapsedFrame = frame;
            return received;
        }
        return carried;
    }

    void NetTestScript::Reset()
    {
        _serverElapsed = -1.0F;
        _frame = 0;
        _stuckFrames = 0;
        _lastPosition = OpenTK::Mathematics::Vector3::Zero;
        _aimDeltaX = 0.0F;
        _aimDeltaY = 0.0F;
        _framesOnTarget = 0;
    }

    void NetTestScript::ApplyOffline(
        std::shared_ptr<Entities::PlayerEntity> player,
        std::int32_t slot,
        std::int32_t frame)
    {
        _offlineSlot = slot;
        _frame = frame;
        Drive(player);
        _offlineSlot = -1;
    }

    void NetTestScript::HoldFire(
        std::shared_ptr<Entities::PlayerEntity> player,
        bool down)
    {
        Entities::PlayerEntity& playerValue = RequireReference(player);
        Entities::PlayerControls& controls = playerValue.Controls();
        Clear(controls);
        Entities::Keybind& shoot = controls.Shoot();
        Hold(shoot, down);
        Finish(playerValue, controls);
    }

    void NetTestScript::LayBombs(
        std::shared_ptr<Entities::PlayerEntity> player,
        std::int32_t frame)
    {
        Entities::PlayerEntity& playerValue = RequireReference(player);
        Entities::PlayerControls& controls = playerValue.Controls();
        Clear(controls);
        if (playerValue.Health() == 0)
        {
            Entities::Keybind& shoot = controls.Shoot();
            Hold(shoot, true);
        }
        else if (!playerValue.IsAltForm() && Settled(playerValue))
        {
            Entities::Keybind& morph = controls.Morph();
            Hold(morph, true);
        }
        else if (playerValue.IsAltForm())
        {
            Entities::Keybind& altAttack = controls.AltAttack();
            const bool down = frame % 20 < 3;
            Hold(altAttack, down);
        }
        Finish(playerValue, controls);
    }

    void NetTestScript::Rest(
        std::shared_ptr<Entities::PlayerEntity> player,
        bool wantBiped)
    {
        Entities::PlayerEntity& playerValue = RequireReference(player);
        Entities::PlayerControls& controls = playerValue.Controls();
        Clear(controls);
        if (playerValue.Health() == 0)
        {
            Entities::Keybind& shoot = controls.Shoot();
            Hold(shoot, true);
        }
        else if (wantBiped && playerValue.IsAltForm() && Settled(playerValue))
        {
            Entities::Keybind& morph = controls.Morph();
            Hold(morph, true);
        }
        Finish(playerValue, controls);
    }

    void NetTestScript::WalkForward(std::shared_ptr<Entities::PlayerEntity> player)
    {
        Entities::PlayerEntity& playerValue = RequireReference(player);
        Entities::PlayerControls& controls = playerValue.Controls();
        Clear(controls);
        if (playerValue.Health() == 0)
        {
            Entities::Keybind& shoot = controls.Shoot();
            Hold(shoot, true);
        }
        else
        {
            Entities::Keybind& moveUp = controls.MoveUp();
            Hold(moveUp, true);
        }
        Finish(playerValue, controls);
    }

    void NetTestScript::Apply(std::shared_ptr<Entities::PlayerEntity> player)
    {
        if (!_enabled)
        {
            return;
        }
        if (HitRig::Active())
        {
            HitRig::Drive(RequireReference(player));
            return;
        }
        IncrementInPlace(_frame);
        Drive(player);
    }

    void NetTestScript::Drive(
        const std::shared_ptr<Entities::PlayerEntity>& player)
    {
        Entities::PlayerEntity& playerValue = RequireReference(player);
        Entities::PlayerControls& controls = playerValue.Controls();
        Clear(controls);
        if (playerValue.Health() == 0)
        {
            Hold(controls.Shoot(), true);
            Finish(playerValue, controls);
            return;
        }
        std::shared_ptr<Entities::PlayerEntity> target = FindTarget(player);
        const bool onTarget = AimAt(playerValue, target);
        const TestPhase phase = Phase();

        if (phase != TestPhase::Zoom
            && playerValue.EquipInfo()->Zoomed
            && _frame % 8 == 0)
        {
            Entities::Keybind& zoom = controls.Zoom();
            Hold(zoom, true);
        }

        switch (phase)
        {
        case TestPhase::Idle:
            break;
        case TestPhase::Walk:
            Square(controls);
            break;
        case TestPhase::Jump:
        {
            Square(controls);
            Entities::Keybind& jump = controls.Jump();
            const bool down = _frame % 45 < 3;
            Hold(jump, down);
            break;
        }
        case TestPhase::Turn:
            _aimDeltaX = 4.0F;
            _aimDeltaY = std::sin(static_cast<float>(_frame) / 40.0F) * 2.0F;
            break;
        case TestPhase::Shoot:
        {
            Entities::Keybind& shoot = controls.Shoot();
            const bool down = _frame % 30 < 20;
            Hold(shoot, down);
            break;
        }
        case TestPhase::SwitchWeapons:
        {
            Entities::Keybind& nextWeapon = controls.NextWeapon();
            const bool nextDown = _frame % 30 == 0;
            Hold(nextWeapon, nextDown);

            Entities::Keybind& shoot = controls.Shoot();
            const bool shootDown = _frame % 30 > 10 && _frame % 30 < 25;
            Hold(shoot, shootDown);
            break;
        }
        case TestPhase::Charge:
        {
            Entities::Keybind& shoot = controls.Shoot();
            Hold(shoot, true);
            break;
        }
        case TestPhase::MorphA:
            MorphOrShoot(playerValue, controls, Even());
            break;
        case TestPhase::AltAttackA:
            AltAttackOrShoot(playerValue, Even(), onTarget);
            break;
        case TestPhase::MorphB:
            MorphOrShoot(playerValue, controls, !Even());
            break;
        case TestPhase::AltAttackB:
            AltAttackOrShoot(playerValue, !Even(), onTarget);
            break;
        case TestPhase::Unmorph:
        {
            Entities::Keybind& morph = controls.Morph();
            bool pressMorph = false;
            if (Settled(playerValue) && playerValue.IsAltForm())
            {
                pressMorph = _frame % 40 == 0;
            }
            Hold(morph, pressMorph);
            Square(controls);
            break;
        }
        case TestPhase::Zoom:
        {
            if (!playerValue.ModCanZoom())
            {
                playerValue.ModArmZoomWeapon();
            }

            Entities::Keybind& zoom = controls.Zoom();
            bool pressZoom = false;
            if (playerValue.ModCanZoom()
                && !playerValue.EquipInfo()->Zoomed)
            {
                pressZoom = _frame % 8 == 0;
            }
            Hold(zoom, pressZoom);

            Entities::Keybind& shoot = controls.Shoot();
            bool shootDown = false;
            if (playerValue.ModCanZoom())
            {
                shootDown = _frame % 40 < 8;
            }
            Hold(shoot, shootDown);
            break;
        }
        case TestPhase::Afflict:
            playerValue.ModArmAffinityWeapon();
            Duel(playerValue, controls, target, onTarget, true);
            break;
        case TestPhase::Duel:
            Duel(playerValue, controls, target, onTarget);
            break;
        case TestPhase::SelfDestruct:
            SelfDestruct(playerValue, controls);
            break;
        }

        Finish(playerValue, controls);
    }

    void NetTestScript::SelfDestruct(Entities::PlayerEntity& player, Entities::PlayerControls& c)
    {
        if (player.IsAltForm() || player.IsMorphing())
        {
            Hold(c.Morph(), Settled(player) && _frame % 40 == 0);
            return;
        }
        static const bool feetMissile = NativeRuntime::EnvironmentGetVariable("MPHREAD_FEET_MISSILE").has_value();
        const ::MphRead::BeamType beam = feetMissile ? ::MphRead::BeamType::Missile : SelfDestructBeam;
        if (player.CurrentWeapon() != beam)
        {
            player.ModArmWeapon(beam);
        }
        const OpenTK::Mathematics::Vector3 ahead(player.Field70(), 0.0F, player.Field74());
        const OpenTK::Mathematics::Vector3 position = player.Position;
        const OpenTK::Mathematics::Vector3 spot = position + OpenTK::Mathematics::Multiply(ahead, 1.0F);
        const auto [turnX, turnY] = player.ModAimDeltaTowards(spot);
        if (!std::isfinite(turnX) || !std::isfinite(turnY))
        {
            return;
        }
        _aimDeltaX = std::clamp(turnX, -TurnRate, TurnRate);
        _aimDeltaY = std::clamp(turnY, -TurnRate, TurnRate);
        const bool aimed = std::abs(turnX) < FiringCone && std::abs(turnY) < FiringCone;
        if (feetMissile)
        {
            FeetMissile(player, c, aimed);
            return;
        }
        if (_releaseFrames > 0)
        {
            _releaseFrames--;
            return;
        }
        if (aimed && player.ModChargeReady())
        {
            _releaseFrames = 4;
            return;
        }
        Hold(c.Shoot(), true);
    }

    void NetTestScript::FeetMissile(Entities::PlayerEntity& player, Entities::PlayerControls& c, bool aimed)
    {
        static_cast<void>(player);
        Hold(c.MoveUp(), true);
        // A tap, so the Missile leaves uncharged.
        Hold(c.Shoot(), aimed && _frame % 30 < 2);
    }

    bool NetTestScript::Settled(Entities::PlayerEntity& player)
    {
        return !player.IsMorphing() && !player.IsUnmorphing();
    }

    bool NetTestScript::Even() noexcept
    {
        const std::int32_t slot = _offlineSlot >= 0
            ? _offlineSlot
            : std::max(NetSession::LocalSlot(), 0);
        return slot % 2 == 0;
    }

    void NetTestScript::MorphOrShoot(
        Entities::PlayerEntity& player,
        Entities::PlayerControls& controls,
        bool morphing)
    {
        if (morphing)
        {
            Entities::Keybind& morph = controls.Morph();
            bool pressMorph = false;
            if (Settled(player) && !player.IsAltForm())
            {
                pressMorph = _frame % 40 == 0;
            }
            Hold(morph, pressMorph);
            Square(controls);

            Entities::Keybind& boost = controls.Boost();
            const bool boostDown = _frame % 60 < 20;
            Hold(boost, boostDown);
            return;
        }

        Entities::Keybind& morph = controls.Morph();
        bool pressMorph = false;
        if (Settled(player) && player.IsAltForm())
        {
            pressMorph = _frame % 40 == 0;
        }
        Hold(morph, pressMorph);

        Entities::Keybind& shoot = controls.Shoot();
        bool shootDown = false;
        if (!player.IsAltForm())
        {
            shootDown = _frame % 30 < 24;
        }
        Hold(shoot, shootDown);
    }

    void NetTestScript::AltAttackOrShoot(
        Entities::PlayerEntity& player,
        bool attacking,
        bool onTarget)
    {
        auto& controls = player.Controls();
        if (attacking)
        {
            // A death during this phase respawns in biped form. Drive the
            // normal morph input until the alt attack becomes reachable.
            // Pressing AltAttack in biped would leave bombs unexercised.
            if (!player.IsAltForm() || !Settled(player))
            {
                MorphOrShoot(player, controls, true);
                return;
            }
            Square(controls);
            Entities::Keybind& altAttack = controls.AltAttack();
            const bool attackDown = _frame % 45 < 6;
            Hold(altAttack, attackDown);
            return;
        }

        Entities::Keybind& shoot = controls.Shoot();
        const bool shootDown = onTarget && _frame % 30 < 24;
        Hold(shoot, shootDown);
    }

    void NetTestScript::Clear(Entities::PlayerControls& controls)
    {
        const std::int32_t initialLength = static_cast<std::int32_t>(controls.All().size());
        if (_wasDown.size() < static_cast<std::size_t>(initialLength))
        {
            _wasDown.assign(static_cast<std::size_t>(initialLength), false);
        }

        for (std::int32_t i = 0; i < static_cast<std::int32_t>(controls.All().size());
            i = UncheckedAdd(i, 1))
        {
            Entities::Keybind& bind = *controls.All()[i];
            _wasDown[CheckedIndex(i, _wasDown.size())]
                = bind.IsDown();
            bind.SetIsDown(false);
            bind.SetIsPressed(false);
            bind.SetIsReleased(false);
        }
    }

    void NetTestScript::Finish(
        Entities::PlayerEntity& player,
        Entities::PlayerControls& controls)
    {
        bool any = false;
        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>(controls.All().size())
                && static_cast<std::size_t>(i) < _wasDown.size();
            i = UncheckedAdd(i, 1))
        {
            Entities::Keybind& bind = *controls.All()[i];
            const bool isDown = bind.IsDown();
            bind.SetIsPressed(isDown && !_wasDown[CheckedIndex(i, _wasDown.size())]);

            const bool isDownForRelease = bind.IsDown();
            bind.SetIsReleased(
                !isDownForRelease && _wasDown[CheckedIndex(i, _wasDown.size())]);

            any |= bind.IsDown()
                || bind.IsReleased();
        }
        if (any)
        {
            player.ModNoteInput();
        }
    }

    bool NetTestScript::AimAt(
        Entities::PlayerEntity& player,
        const std::shared_ptr<Entities::PlayerEntity>& target)
    {
        _aimDeltaX = 0.0F;
        _aimDeltaY = 0.0F;
        if (!target)
        {
            return false;
        }

        Entities::PlayerEntity& targetValue = RequireReference(target);
        const OpenTK::Mathematics::Vector3 aimTarget
            = targetValue.ModAimTarget();
        const std::pair<float, float> turn
            = player.ModAimDeltaTowards(aimTarget);
        const float turnX = turn.first;
        const float turnY = turn.second;

        if (!std::isfinite(turnX) || !std::isfinite(turnY))
        {
            return false;
        }

        _aimDeltaX = std::clamp(turnX, -TurnRate, TurnRate);
        _aimDeltaY = std::clamp(turnY, -TurnRate, TurnRate);
        const bool onTarget
            = std::fabs(turnX) < FiringCone && std::fabs(turnY) < FiringCone;
        if (onTarget)
        {
            IncrementInPlace(_framesOnTarget);
        }
        return onTarget;
    }

    void NetTestScript::Square(Entities::PlayerControls& controls)
    {
        const std::int32_t phase = _frame / 60 % 4;

        Entities::Keybind& moveUp = controls.MoveUp();
        const bool up = phase == 0;
        Hold(moveUp, up);

        Entities::Keybind& moveRight = controls.MoveRight();
        const bool right = phase == 1;
        Hold(moveRight, right);

        Entities::Keybind& moveDown = controls.MoveDown();
        const bool down = phase == 2;
        Hold(moveDown, down);

        Entities::Keybind& moveLeft = controls.MoveLeft();
        const bool left = phase == 3;
        Hold(moveLeft, left);
    }

    void NetTestScript::Duel(
        Entities::PlayerEntity& player,
        Entities::PlayerControls& controls,
        const std::shared_ptr<Entities::PlayerEntity>& target,
        bool onTarget,
        bool charged)
    {
        if (!target)
        {
            Square(controls);
            Entities::Keybind& shoot = controls.Shoot();
            const bool shootDown = _frame % 60 < 20;
            Hold(shoot, shootDown);
            return;
        }

        Entities::PlayerEntity& targetValue = RequireReference(target);
        const OpenTK::Mathematics::Vector3 targetPosition = targetValue.Position;
        const OpenTK::Mathematics::Vector3 playerPositionForDistance = player.Position;
        const float distance = Length(targetPosition - playerPositionForDistance);

        const OpenTK::Mathematics::Vector3 playerPositionForMoved = player.Position;
        const float moved = Length(playerPositionForMoved - _lastPosition);
        _lastPosition = static_cast<OpenTK::Mathematics::Vector3>(player.Position);
        _stuckFrames = moved < 0.02F
            ? UncheckedAdd(_stuckFrames, 1)
            : 0;
        const bool stuck = _stuckFrames > 20;
        if (stuck && _stuckFrames > 90)
        {
            _stuckFrames = 0;
            _stuckDirection = !_stuckDirection;
        }

        Entities::Keybind& moveUp = controls.MoveUp();
        const bool moveUpDown = !stuck && distance > PreferredRange;
        Hold(moveUp, moveUpDown);

        Entities::Keybind& moveDown = controls.MoveDown();
        const bool moveDownDown = !stuck && distance < PreferredRange / 2.0F;
        Hold(moveDown, moveDownDown);

        Entities::Keybind& moveLeft = controls.MoveLeft();
        const bool moveLeftDown = stuck
            ? _stuckDirection
            : distance <= PreferredRange && _frame / 90 % 2 == 0;
        Hold(moveLeft, moveLeftDown);

        Entities::Keybind& moveRight = controls.MoveRight();
        const bool moveRightDown = stuck
            ? !_stuckDirection
            : distance <= PreferredRange && _frame / 90 % 2 == 1;
        Hold(moveRight, moveRightDown);

        Entities::Keybind& jump = controls.Jump();
        const bool jumpDown
            = stuck ? _stuckFrames % 30 < 3 : _frame % 150 < 3;
        Hold(jump, jumpDown);

        if (!charged)
        {
            Entities::Keybind& shoot = controls.Shoot();
            const bool shootDown = onTarget && _frame % 30 < 24;
            Hold(shoot, shootDown);
            return;
        }

        if (_releaseFrames > 0)
        {
            DecrementInPlace(_releaseFrames);
            return;
        }
        if (onTarget && player.ModChargeReady())
        {
            _releaseFrames = 4;
            return;
        }

        Entities::Keybind& shoot = controls.Shoot();
        Hold(shoot, true);
    }

    std::shared_ptr<Entities::PlayerEntity> NetTestScript::FindTarget(
        const std::shared_ptr<Entities::PlayerEntity>& self)
    {
        if (NetSession::Active()
            && RequireReference(self).SlotIndex() >= 0)
        {
            const std::int32_t count = static_cast<std::int32_t>(
                Entities::PlayerEntity::Players().size());
            for (std::int32_t step = 1; step < count;
                step = UncheckedAdd(step, 1))
            {
                const std::int32_t targetSlot
                    = UncheckedAdd(RequireReference(self).SlotIndex(), step) % count;
                const auto& players = Entities::PlayerEntity::Players();
                const std::shared_ptr<Entities::PlayerEntity> target
                    = players[CheckedIndex(targetSlot, players.size())];
                if (target != self)
                {
                    Entities::PlayerEntity& targetValue = RequireReference(target);
                    if (TestFlag(targetValue.LoadFlags(), Entities::LoadFlags::Active)
                        && TestFlag(targetValue.LoadFlags(), Entities::LoadFlags::Spawned)
                        && targetValue.Health() > 0)
                    {
                        return target;
                    }
                }
            }
        }

        std::shared_ptr<Entities::PlayerEntity> best{};
        float bestDistance = std::numeric_limits<float>::max();
        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>(Entities::PlayerEntity::Players().size());
            i = UncheckedAdd(i, 1))
        {
            const auto& players = Entities::PlayerEntity::Players();
            const std::shared_ptr<Entities::PlayerEntity> other
                = players[CheckedIndex(i, players.size())];
            if (other == self)
            {
                continue;
            }

            Entities::PlayerEntity& otherValue = RequireReference(other);
            if (!TestFlag(otherValue.LoadFlags(), Entities::LoadFlags::Active)
                || !TestFlag(otherValue.LoadFlags(), Entities::LoadFlags::Spawned)
                || otherValue.Health() == 0)
            {
                continue;
            }

            const OpenTK::Mathematics::Vector3 otherPosition = otherValue.Position;
            const OpenTK::Mathematics::Vector3 selfPosition = RequireReference(self).Position;
            const float distance = Length(otherPosition - selfPosition);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = other;
            }
        }
        return best;
    }

    void NetTestScript::Hold(Entities::Keybind& bind, bool down)
    {
        bind.SetIsDown(down);
    }
}

namespace MphRead::Mods::Network
{
    namespace
    {
        // NetTestScript.cs TestPhase : int
        constexpr ::MphRead::NativeRuntime::EnumNameEntry TestPhaseNames[] = {
            {0ULL, "Idle"},
            {1ULL, "Walk"},
            {2ULL, "Jump"},
            {3ULL, "Turn"},
            {4ULL, "Shoot"},
            {5ULL, "SwitchWeapons"},
            {6ULL, "Charge"},
            {7ULL, "MorphA"},
            {8ULL, "AltAttackA"},
            {9ULL, "MorphB"},
            {10ULL, "AltAttackB"},
            {11ULL, "Unmorph"},
            {12ULL, "Zoom"},
            {13ULL, "Afflict"},
            {14ULL, "Duel"},
            {15ULL, "SelfDestruct"},
        };
    }

    std::string ToString(TestPhase value)
    {
        return ::MphRead::NativeRuntime::ManagedEnumToString(
            value, TestPhaseNames, std::size(TestPhaseNames), false);
    }
}
