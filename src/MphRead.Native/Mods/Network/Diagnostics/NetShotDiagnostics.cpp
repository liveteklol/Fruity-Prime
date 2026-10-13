#include "Mods/Network/Diagnostics/NetShotDiagnostics.hpp"

#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Enum.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::string ToString(ShotAttemptResult value)
    {
        static constexpr std::array<Runtime::EnumNameEntry, 10> Names = {{
            {0, "Spawned"}, {1, "AttachedEnemy"}, {2, "Cooldown"}, {3, "AutofireCooldown"},
            {4, "GunLowered"}, {5, "NoAmmo"}, {6, "NoProjectileSlot"}, {7, "DeadOrNotInPlay"},
            {8, "StaleLife"}, {9, "OtherNoSpawn"}}};
        return Runtime::ManagedEnumToString(value, Names.data(), Names.size(), false);
    }

    ShotKey ShotKey::For(std::int32_t slot, std::uint32_t frame)
    {
        return ShotKey(NetSession::AuthorityEpoch(), NetSession::CurrentMatchId(), slot,
            NetPlayerLifecycle::Generation(slot), NetPlayerLifecycle::Get(slot), frame);
    }

    std::string ShotKey::ToString() const
    {
        return Runtime::ToString(AuthorityEpoch) + "/" + Runtime::ToString(MatchId) + "/"
            + Runtime::ToString(ShooterSlot) + "/" + Runtime::ToString(Generation) + "/"
            + Runtime::ToString(LifeId) + "/" + Runtime::ToString(LaunchFrame);
    }

    NetShotDiagnostics::Snapshot NetShotDiagnostics::_snapshot{};

    const NetShotDiagnostics::Snapshot& NetShotDiagnostics::Read() noexcept
    {
        return _snapshot;
    }

    void NetShotDiagnostics::Continuous(::MphRead::Entities::BeamProjectileEntity& beam, std::int32_t ammo)
    {
        const auto b = static_cast<std::size_t>(Bucket(beam.Beam()));
        Runtime::IncrementInPlace(_snapshot.ContinuousTicks[b]);
        if (beam.Damage() > 0)
        {
            Runtime::IncrementInPlace(_snapshot.ContinuousDamageTicks[b]);
        }
        if (beam.Target() != nullptr)
        {
            Runtime::IncrementInPlace(_snapshot.ContinuousAcquired[b]);
        }
        _snapshot.ContinuousAmmo[b] = Runtime::UncheckedAdd(_snapshot.ContinuousAmmo[b], ammo);
        if (NetLog::Enabled())
        {
            Trace("continuous", beam.ModLaunchKey(), beam.Beam(),
                "phase=" + Runtime::ToString(beam.ModContinuousPhase) + " damage="
                + Runtime::ToString(beam.Damage()) + " ammo=" + Runtime::ToString(ammo)
                + " acquired=" + (beam.Target() != nullptr ? "True" : "False"));
        }
    }

    void NetShotDiagnostics::RecordPrediction(::MphRead::BeamType beam, std::int64_t damage, bool headshot) noexcept
    {
        const auto weapon = static_cast<std::size_t>(Bucket(beam));
        Runtime::IncrementInPlace(_snapshot.LocalHits[weapon]);
        Runtime::IncrementInPlace(_snapshot.Predictions[weapon]);
        _snapshot.PredictedDamage[weapon] = Runtime::UncheckedAdd(_snapshot.PredictedDamage[weapon], damage);
        if (headshot)
        {
            Runtime::IncrementInPlace(_snapshot.LocalHeadshots[weapon]);
        }
    }

    void NetShotDiagnostics::RecordAuthorityHit(::MphRead::BeamType beam, std::int64_t damage,
        bool headshot) noexcept
    {
        const auto weapon = static_cast<std::size_t>(Bucket(beam));
        Runtime::IncrementInPlace(_snapshot.AuthorityHits[weapon]);
        _snapshot.AuthorityDamage[weapon] = Runtime::UncheckedAdd(_snapshot.AuthorityDamage[weapon], damage);
        if (headshot)
        {
            Runtime::IncrementInPlace(_snapshot.AuthorityHeadshots[weapon]);
        }
    }

    void NetShotDiagnostics::RecordClaim(::MphRead::BeamType beam) noexcept
    {
        Runtime::IncrementInPlace(_snapshot.Claims[static_cast<std::size_t>(Bucket(beam))]);
    }

    void NetShotDiagnostics::RecordRescue(::MphRead::BeamType beam) noexcept
    {
        Runtime::IncrementInPlace(_snapshot.Rescues[static_cast<std::size_t>(Bucket(beam))]);
    }

    void NetShotDiagnostics::RecordRefusal(::MphRead::BeamType beam) noexcept
    {
        Runtime::IncrementInPlace(_snapshot.Refusals[static_cast<std::size_t>(Bucket(beam))]);
    }

    void NetShotDiagnostics::RecordRewind(::MphRead::BeamType beam, std::int32_t frames, bool clamped) noexcept
    {
        const auto weapon = static_cast<std::size_t>(Bucket(beam));
        Runtime::IncrementInPlace(_snapshot.RewindSamples[weapon]);
        _snapshot.RewindFrames[weapon] = Runtime::UncheckedAdd(_snapshot.RewindFrames[weapon], frames);
        if (clamped)
        {
            Runtime::IncrementInPlace(_snapshot.RewindClamps[weapon]);
        }
    }

    void NetShotDiagnostics::RecordDrainCredit(std::int64_t amount) noexcept
    {
        const auto weapon = static_cast<std::size_t>(Bucket(::MphRead::BeamType::ShockCoil));
        _snapshot.DrainCredit[weapon] = Runtime::UncheckedAdd(_snapshot.DrainCredit[weapon], amount);
    }

    std::int32_t NetShotDiagnostics::Bucket(::MphRead::BeamType weapon) noexcept
    {
        const auto value = static_cast<std::int32_t>(weapon);
        return value >= 0 && value < WeaponCount - 1 ? value : WeaponCount - 1;
    }

    void NetShotDiagnostics::Trace(std::string_view stage, const ShotKey& key,
        ::MphRead::BeamType weapon, std::string_view detail)
    {
        if (NetLog::Enabled())
        {
            NetLog::Event("[shot] key=" + key.ToString() + " stage=" + std::string(stage)
                + " weapon=" + ::MphRead::ToString(weapon) + " authorityFrame="
                + Runtime::ToString(NetSession::NetFrame()) + " " + std::string(detail));
        }
    }

    bool NetShotDiagnostics::Finish(::MphRead::Entities::PlayerEntity& shooter,
        ShotAttemptResult result, OpenTK::Mathematics::Vector3 shot, OpenTK::Mathematics::Vector3 aim)
    {
        if (NetSession::Active())
        {
            Runtime::IncrementInPlace(_snapshot.Outcomes[static_cast<std::size_t>(Bucket(shooter.CurrentWeapon()))]
                [static_cast<std::size_t>(result)]);
            if (result == ShotAttemptResult::Spawned)
            {
                NetDamage::NoteFired(shooter, shot, aim);
            }
            if (NetLog::Enabled())
            {
                const std::int32_t slot = shooter.SlotIndex();
                Trace("attempt", ShotKey::For(slot, NetUnlagged::LaunchFrameFor(shooter)),
                    shooter.CurrentWeapon(), "result=" + ToString(result)
                    + " health=" + Runtime::ToString(shooter.Health())
                    + " shoot=" + (shooter.Controls().Shoot().IsDown() ? "True" : "False")
                    + " press=" + (shooter.Controls().Shoot().IsPressed() ? "True" : "False")
                    + " pressAge=" + Runtime::ToString(NetPlayerBridge::ShootPressAgeFor(slot)));
            }
        }
        return result == ShotAttemptResult::Spawned;
    }

    void NetShotDiagnostics::Reset()
    {
        for (auto& row : _snapshot.Outcomes)
        {
            row.fill(0);
        }
        for (Counts* values : {&_snapshot.LocalHits, &_snapshot.AuthorityHits, &_snapshot.Predictions, &_snapshot.Claims, &_snapshot.Rescues, &_snapshot.Refusals,
                 &_snapshot.PredictedDamage, &_snapshot.AuthorityDamage, &_snapshot.LocalHeadshots, &_snapshot.AuthorityHeadshots, &_snapshot.RewindSamples,
                 &_snapshot.RewindFrames, &_snapshot.RewindClamps, &_snapshot.ContinuousTicks, &_snapshot.ContinuousDamageTicks, &_snapshot.ContinuousAcquired,
                 &_snapshot.ContinuousAmmo, &_snapshot.DrainCredit})
        {
            values->fill(0);
        }
    }

    std::string NetShotDiagnostics::Describe()
    {
        std::string text = "weapon attempted spawned localHits authorityHits predictions claims rescues "
            "refusals predictedDamage authorityDamage localHeadshots authorityHeadshots meanRewind clamp%\n";
        for (std::int32_t b = 0; b < WeaponCount; b++)
        {
            const auto i = static_cast<std::size_t>(b);
            std::int64_t attempted = 0;
            for (std::size_t r = 0; r < ResultCount; r++)
            {
                attempted = Runtime::UncheckedAdd(attempted, _snapshot.Outcomes[i][r]);
            }
            const std::int64_t observed = Runtime::UncheckedAdd(
                Runtime::UncheckedAdd(Runtime::UncheckedAdd(attempted, _snapshot.LocalHits[i]), _snapshot.AuthorityHits[i]), _snapshot.Claims[i]);
            if (observed == 0)
            {
                continue;
            }
            const double meanRewind = _snapshot.RewindSamples[i] == 0 ? 0.0
                : static_cast<double>(_snapshot.RewindFrames[i]) / static_cast<double>(_snapshot.RewindSamples[i]);
            const double clamp = _snapshot.RewindSamples[i] == 0 ? 0.0
                : 100.0 * static_cast<double>(_snapshot.RewindClamps[i]) / static_cast<double>(_snapshot.RewindSamples[i]);
            text += (b == WeaponCount - 1 ? std::string("alt/bomb")
                : ::MphRead::ToString(static_cast<::MphRead::BeamType>(b)))
                + " " + Runtime::ToString(attempted) + " " + Runtime::ToString(_snapshot.Outcomes[i][0])
                + " " + Runtime::ToString(_snapshot.LocalHits[i]) + " " + Runtime::ToString(_snapshot.AuthorityHits[i])
                + " " + Runtime::ToString(_snapshot.Predictions[i]) + " " + Runtime::ToString(_snapshot.Claims[i])
                + " " + Runtime::ToString(_snapshot.Rescues[i]) + " " + Runtime::ToString(_snapshot.Refusals[i])
                + " " + Runtime::ToString(_snapshot.PredictedDamage[i]) + " " + Runtime::ToString(_snapshot.AuthorityDamage[i])
                + " " + Runtime::ToString(_snapshot.LocalHeadshots[i]) + " " + Runtime::ToString(_snapshot.AuthorityHeadshots[i])
                + " " + Runtime::ToString(meanRewind, "F2") + " " + Runtime::ToString(clamp, "F1")
                + std::string(Runtime::EnvironmentNewLine());
            if (_snapshot.ContinuousTicks[i] > 0)
            {
                text += "  continuous ticks=" + Runtime::ToString(_snapshot.ContinuousTicks[i])
                    + " nonzero=" + Runtime::ToString(_snapshot.ContinuousDamageTicks[i])
                    + " acquired=" + Runtime::ToString(_snapshot.ContinuousAcquired[i])
                    + " ammo=" + Runtime::ToString(_snapshot.ContinuousAmmo[i])
                    + " drainCredit=" + Runtime::ToString(_snapshot.DrainCredit[i]) + std::string(Runtime::EnvironmentNewLine());
            }
            for (std::size_t r = 1; r < ResultCount; r++)
            {
                if (_snapshot.Outcomes[i][r] != 0)
                {
                    text += "  " + ToString(static_cast<ShotAttemptResult>(r)) + "="
                        + Runtime::ToString(_snapshot.Outcomes[i][r]) + std::string(Runtime::EnvironmentNewLine());
                }
            }
        }
        return text;
    }
}
