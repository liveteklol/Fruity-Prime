#include "Mods/Network/Diagnostics/HomingShotTrace.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/DebugLog.hpp"
#include "Mods/Network/Gameplay/Weapons/FiringVariant.hpp"
#include "Mods/Network/Gameplay/Weapons/HomingTargetAcquisition.hpp"
#include "Mods/Network/Gameplay/Weapons/ShotOwnership.hpp"
#include "Mods/Network/Gameplay/Weapons/HomingTargetReference.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <sstream>
#include <utility>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using OpenTK::Mathematics::Vector3;

    namespace
    {
        // The tables store per-tick values in 4096ths; a projectile holds
        // them halved for a step twice as frequent.
        constexpr float RawPerProjectileUnit = 4096.0F * 2.0F;

        [[nodiscard]] std::string VectorText(Vector3 value)
        {
            return Runtime::ToString(value.X, "F6") + "," + Runtime::ToString(value.Y, "F6")
                + "," + Runtime::ToString(value.Z, "F6");
        }

        [[nodiscard]] HomingShotRole RoleFor(const Entities::EntityBase* owner) noexcept
        {
            if (!NetSession::Active())
            {
                return HomingShotRole::Offline;
            }
            if (owner == nullptr || owner->Type != EntityType::Player)
            {
                return ShotOwnership::Judges() ? HomingShotRole::Authority : HomingShotRole::Observer;
            }
            if (ShotOwnership::IsOwner(*static_cast<const Entities::PlayerEntity*>(owner)))
            {
                return HomingShotRole::Owner;
            }
            return ShotOwnership::Judges() ? HomingShotRole::Authority : HomingShotRole::Observer;
        }

        [[nodiscard]] float AngleDegrees(Vector3 a, Vector3 b) noexcept
        {
            const float lengths = std::sqrt(Vector3::Dot(a, a) * Vector3::Dot(b, b));
            if (!(lengths > 0.0F))
            {
                return 0.0F;
            }
            const float cosine = std::clamp(Vector3::Dot(a, b) / lengths, -1.0F, 1.0F);
            return std::acos(cosine) * 180.0F / std::numbers::pi_v<float>;
        }
    }

    std::string ToString(HomingShotRole value)
    {
        switch (value)
        {
        case HomingShotRole::Offline: return "offline";
        case HomingShotRole::Owner: return "owner";
        case HomingShotRole::Authority: return "authority";
        case HomingShotRole::Observer: return "observer";
        }
        return std::to_string(static_cast<std::int32_t>(value));
    }

    void HomingShotTrace::Enable(bool value) noexcept
    {
        _enabled = value;
    }

    bool HomingShotTrace::Enabled() noexcept
    {
        return _enabled;
    }

    void HomingShotTrace::SetSpawnSink(SpawnSink sink)
    {
        _spawnSink = std::move(sink);
    }

    bool HomingShotTrace::Capturing() noexcept
    {
        return _enabled || static_cast<bool>(_spawnSink);
    }

    void HomingShotTrace::Line(const std::string& text)
    {
        if (_enabled)
        {
            DebugLog::Line("homing", text);
        }
    }

    void HomingShotTrace::Spawned(const Entities::BeamProjectileEntity& beam,
        const EquipInfo& equip, std::int32_t projectile, bool muzzleBlocked)
    {
        if (!Capturing())
        {
            return;
        }
        const std::shared_ptr<Entities::EntityBase> owner = beam.Owner();
        HomingShotRecord record{};
        record.Role = RoleFor(owner.get());
        record.OwnerType = owner != nullptr ? owner->Type : EntityType{};
        record.Projectile = projectile;
        record.Frame = NetSession::Active() ? NetSession::NetFrame() : 0U;
        record.Beam = beam.Beam();
        record.WeaponRow = WeaponRow(equip.Weapon.get());
        record.Charge = equip.ChargeLevel;
        record.Charged = ::MphRead::TestFlag(beam.Flags(), Entities::BeamFlags::Charged);
        record.HomingRaw = beam.Homing() * RawPerProjectileUnit;
        record.SpeedRaw = beam.Speed() * RawPerProjectileUnit;
        record.RawOrigin = beam.SpawnPosition();
        record.RawRay = beam.Direction();
        record.Velocity = beam.Velocity();
        if (equip.Weapon != nullptr)
        {
            record.RangeLimit = Fixed::ToFloat(equip.Weapon->HomingRange);
            record.CosLimit = Fixed::ToFloat(equip.HomingTolerance());
        }
        record.Homing = ::MphRead::TestFlag(beam.Flags(), Entities::BeamFlags::Homing);
        record.Continuous = ::MphRead::TestFlag(beam.Flags(), Entities::BeamFlags::Continuous);
        record.MuzzleBlocked = muzzleBlocked;
        record.Target = HomingTargetReference::Encode(beam.Target().get());
        record.Acquisition = ToString(HomingTargetAcquisition::ConsumeOutcome());
        record.Presentation = beam.ModPresentation.Active();
        record.RetargetDegrees = record.Presentation
            ? AngleDegrees(beam.ModPresentation.Velocity(), beam.Velocity()) : 0.0F;
        if (owner != nullptr && owner->Type == EntityType::Player)
        {
            const auto& player = static_cast<const Entities::PlayerEntity&>(*owner);
            record.OwnerSlot = player.SlotIndex();
            record.Generation = beam.ModLaunchGeneration;
            record.LifeId = beam.ModLaunchLife;
            record.Sequence = beam.ModShotSequence;
            record.IntentFrame = record.Role == HomingShotRole::Owner || !NetSession::HasRemoteIntent(record.OwnerSlot)
                ? record.Frame
                : NetSession::RemoteIntent(static_cast<std::size_t>(record.OwnerSlot)).Frame;
            const std::optional<IntentPacket::ShotEvent> event = NetShotEvents::CurrentShotEvent(player);
            if (event.has_value() && event->HasRay())
            {
                record.RawOrigin = event->Origin;
                record.RawRay = event->Direction;
            }
            else if (record.Role != HomingShotRole::Owner && NetSession::HasRemoteIntent(record.OwnerSlot))
            {
                const auto& intent = NetSession::RemoteIntent(static_cast<std::size_t>(record.OwnerSlot));
                if (intent.HasShot)
                {
                    record.RawOrigin = intent.ShotOrigin;
                    record.RawRay = intent.ShotDirection;
                }
            }
            record.PrimeHunter = event.has_value() ? event->PrimeHunterAtFire()
                : FiringVariant::For(player).PrimeHunter;
        }
        if (const auto target = beam.Target(); target != nullptr)
        {
            Vector3 position{};
            target->GetPosition(position);
            const Vector3 relative = position - beam.SpawnPosition();
            record.TargetDistance = std::sqrt(Vector3::Dot(relative, relative));
            const float length = record.TargetDistance * std::sqrt(Vector3::Dot(record.Velocity, record.Velocity));
            if (length > 0.0F)
            {
                record.TargetCos = std::clamp(Vector3::Dot(relative, record.Velocity) / length, -1.0F, 1.0F);
                record.HasTargetGeometry = true;
            }
        }
        if (_spawnSink)
        {
            _spawnSink(record, beam);
        }
        Line(Format(record));
    }

    void HomingShotTrace::Claimed(const HomingClaimRecord& record)
    {
        if (!_enabled)
        {
            return;
        }
        std::ostringstream line;
        line << "claim shooter=" << record.ShooterSlot << " claim=" << record.ClaimId
             << " seq=" << record.Sequence << " beam=" << static_cast<std::int32_t>(record.Beam)
             << " victim=" << static_cast<std::int32_t>(record.VictimSlot)
             << " row=" << record.WeaponRow << " straight=" << (record.Straight ? 1 : 0);
        Line(line.str());
    }

    void HomingShotTrace::Answered(const HomingVerdictRecord& record)
    {
        if (!_enabled)
        {
            return;
        }
        std::ostringstream line;
        line << "verdict shooter=" << record.ShooterSlot << " claim=" << record.ClaimId
             << " result=" << HitVerdictPacket::Describe(record.Result);
        Line(line.str());
    }

    void HomingShotTrace::Drained(const HomingDrainRecord& record)
    {
        if (!_enabled)
        {
            return;
        }
        std::ostringstream line;
        line << "drain role=" << ToString(record.Role) << " shooter=" << record.ShooterSlot
             << " victim=" << record.VictimSlot << " claim=" << record.ClaimId
             << " amount=" << record.Amount << " row=" << record.WeaponRow;
        Line(line.str());
    }

    std::string HomingShotTrace::WeaponRow(const WeaponInfo* weapon)
    {
        if (weapon == nullptr)
        {
            return "-";
        }
        const std::array<std::pair<const char*, const std::shared_ptr<const Weapons::WeaponList>*>, 7> tables{{
            {"1P", &Weapons::Weapons1P},
            {"MP", &Weapons::WeaponsMP},
            {"Enemy", &Weapons::EnemyWeapons},
            {"Boss", &Weapons::BossWeapons},
            {"Gorea", &Weapons::GoreaWeapons},
            {"Platform", &Weapons::PlatformWeapons},
            {"Ricochet", &Weapons::Ricochets},
        }};
        for (const auto& [name, table] : tables)
        {
            if (*table == nullptr)
            {
                continue;
            }
            const Weapons::WeaponList& rows = **table;
            for (std::size_t i = 0; i < rows.size(); ++i)
            {
                if (rows[i].get() == weapon)
                {
                    return std::string(name) + ":" + std::to_string(i);
                }
            }
        }
        return "?";
    }

    std::string HomingShotTrace::Describe(const IntentPacket::ShotTargetReference& target)
    {
        using Kind = IntentPacket::ShotTargetKind;
        std::ostringstream text;
        switch (target.Kind)
        {
        case Kind::Player:
        case Kind::Halfturret:
            text << (target.Kind == Kind::Player ? 'P' : 'H') << static_cast<std::int32_t>(target.Slot)
                 << ':' << target.Generation << ':' << target.LifeId;
            break;
        case Kind::SceneEntity:
            text << 'S' << static_cast<std::int32_t>(target.EntityType) << ':' << target.EntityId;
            break;
        case Kind::EnemyInstance:
            text << 'E' << std::hex << target.StableId;
            break;
        default:
            text << '-';
            break;
        }
        return text.str();
    }

    std::string HomingShotTrace::Format(const HomingShotRecord& record)
    {
        std::ostringstream line;
        line << "spawn role=" << ToString(record.Role)
             << " owner=" << ::MphRead::ToString(record.OwnerType)
             << " slot=" << record.OwnerSlot << " gen=" << record.Generation
             << " life=" << record.LifeId << " seq=" << record.Sequence
             << " frame=" << record.Frame << " intent=" << record.IntentFrame
             << " proj=" << record.Projectile
             << " beam=" << ::MphRead::ToString(record.Beam) << " row=" << record.WeaponRow
             << " charge=" << record.Charge << " charged=" << (record.Charged ? 1 : 0)
             << " prime=" << (record.PrimeHunter ? 1 : 0)
             << " homing=" << Runtime::ToString(record.HomingRaw, "F3")
             << " speed=" << Runtime::ToString(record.SpeedRaw, "F3")
             << " rawOrigin=" << VectorText(record.RawOrigin)
             << " rawRay=" << VectorText(record.RawRay)
             << " velocity=" << VectorText(record.Velocity)
             << " range=" << Runtime::ToString(record.RangeLimit, "F6")
             << " cosLimit=" << Runtime::ToString(record.CosLimit, "F6")
             << " targetDistance=" << (record.HasTargetGeometry ? Runtime::ToString(record.TargetDistance, "F6") : "-")
             << " targetCos=" << (record.HasTargetGeometry ? Runtime::ToString(record.TargetCos, "F6") : "-")
             << " steer=" << (record.Homing ? 1 : 0)
             << " continuous=" << (record.Continuous ? 1 : 0)
             << " muzzleblocked=" << (record.MuzzleBlocked ? 1 : 0)
             << " target=" << Describe(record.Target)
             << " acquired=" << record.Acquisition
             << " presentation=" << (record.Presentation ? 1 : 0)
             << " retarget=" << Runtime::ToString(record.RetargetDegrees, "F3");
        return line.str();
    }
}
