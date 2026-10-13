#pragma once

#include "Formats/Enums.hpp"
#include "Mods/Network/Domain/ShotKey.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace MphRead::Entities
{
    class BeamProjectileEntity;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    enum class ShotAttemptResult : std::int32_t
    {
        Spawned, AttachedEnemy, Cooldown, AutofireCooldown, GunLowered, NoAmmo,
        NoProjectileSlot, DeadOrNotInPlay, StaleLife, OtherNoSpawn
    };

    // ShotAttemptResult.ToString().
    [[nodiscard]] std::string ToString(ShotAttemptResult value);

    class NetShotDiagnostics final
    {
    public:
        NetShotDiagnostics() = delete;

        static constexpr std::int32_t WeaponCount = static_cast<std::int32_t>(::MphRead::BeamType::Enemy) + 2;
        static constexpr std::int32_t ResultCount = 10;
        using Counts = std::array<std::int64_t, WeaponCount>;

        struct Snapshot final
        {
            std::array<std::array<std::int64_t, ResultCount>, WeaponCount> Outcomes{};
            Counts LocalHits{};
            Counts AuthorityHits{};
            Counts Predictions{};
            Counts Claims{};
            Counts Rescues{};
            Counts Refusals{};
            Counts PredictedDamage{};
            Counts AuthorityDamage{};
            Counts LocalHeadshots{};
            Counts AuthorityHeadshots{};
            Counts RewindSamples{};
            Counts RewindFrames{};
            Counts RewindClamps{};
            Counts ContinuousTicks{};
            Counts ContinuousDamageTicks{};
            Counts ContinuousAcquired{};
            Counts ContinuousAmmo{};
            Counts DrainCredit{};
        };

        [[nodiscard]] static const Snapshot& Read() noexcept;

        static void Continuous(::MphRead::Entities::BeamProjectileEntity& beam, std::int32_t ammo);
        static void RecordPrediction(::MphRead::BeamType beam, std::int64_t damage, bool headshot) noexcept;
        static void RecordAuthorityHit(::MphRead::BeamType beam, std::int64_t damage, bool headshot) noexcept;
        static void RecordClaim(::MphRead::BeamType beam) noexcept;
        static void RecordRescue(::MphRead::BeamType beam) noexcept;
        static void RecordRefusal(::MphRead::BeamType beam) noexcept;
        static void RecordRewind(::MphRead::BeamType beam, std::int32_t frames, bool clamped) noexcept;
        static void RecordDrainCredit(std::int64_t amount) noexcept;
        [[nodiscard]] static std::int32_t Bucket(::MphRead::BeamType weapon) noexcept;
        static void Trace(std::string_view stage, const ShotKey& key, ::MphRead::BeamType weapon,
            std::string_view detail = {});
        // Exactly one outcome for every TryFireWeapon invocation, including
        // early returns.
        static bool Finish(::MphRead::Entities::PlayerEntity& shooter, ShotAttemptResult result,
            OpenTK::Mathematics::Vector3 shot = {}, OpenTK::Mathematics::Vector3 aim = {});
        static void Reset();
        [[nodiscard]] static std::string Describe();

    private:
        static Snapshot _snapshot;
    };
}
