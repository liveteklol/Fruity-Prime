#pragma once

#include "Formats/Types.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

namespace MphRead::Entities
{
    class BeamProjectileEntity;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Where on a hunter a shot landed, written by every machine that sees it:
    // the shooter's prediction, the authority's claim (and its own suppressed
    // copy), and the victim's drawn impact and the damage that reached it.
    // One CSV line per event (-hitlog FILE); tools/hitrig/hitloc.py joins the
    // three points of view on the shot key and says how far apart they were.
    //
    // The body frame: dy is the height above Position (the capsule runs
    // MinPickupHeight..MaxPickupHeight, -0.5..1.1), hpct the same as a share
    // of it (the headshot band is the top 18.75%), ang the bearing around the
    // body from the way the victim faces (0 = front, +90 = their right) and
    // rad the horizontal distance from the axis.
    class HitLocation final
    {
    public:
        HitLocation() = delete;

        static void Open(const std::string& path);
        static void Close();
        [[nodiscard]] static bool Enabled() noexcept { return _writer != nullptr; }

        // A projectile (or its splash) reached a player in this machine's
        // own simulation. Which point of view it is follows from the roles:
        // a local shooter (the shooter's view), a local victim (the victim's
        // view), or the authority (its own copy of a remote player's shot).
        // blocked: the victim was invulnerable here, so this machine dealt
        // nothing for it (and a shooter claims nothing).
        static void Contact(const Entities::BeamProjectileEntity& beam, const Entities::PlayerEntity& victim,
            OpenTK::Mathematics::Vector3 point, bool headshot, std::uint32_t damage, bool splash, bool blocked);
        // A remote shot drawn here came closest to this machine's player
        // without touching them: how far, and where on the body it passed.
        static void NearMiss(const Entities::BeamProjectileEntity& beam, const Entities::PlayerEntity& victim,
            OpenTK::Mathematics::Vector3 point, float distance);
        // The authority applied (or refused) a claim: the victim as the
        // shooter drew them, as the authority's history had them at that
        // frame, and where they are now.
        static void Claim(std::int32_t shooter, const Entities::PlayerEntity& victim, std::uint8_t beam,
            std::uint32_t launch, std::int32_t damage, std::uint8_t flags, OpenTK::Mathematics::Vector3 drawn,
            OpenTK::Mathematics::Vector3 history, bool historyKnown, std::int32_t verdict,
            const std::string& checks = std::string());
        // An unconfirmed remote shot met this machine's player and was let through.
        static void Passed(const Entities::BeamProjectileEntity& beam, const Entities::PlayerEntity& victim,
            OpenTK::Mathematics::Vector3 point);
        // An impact drawn on this machine's player at the spot the shooter
        // saw, for a confirmed shot that was not (or no longer) in the air here.
        static void Synthesized(std::int32_t attacker, const Entities::PlayerEntity& victim, std::uint8_t beam,
            std::uint32_t launch, OpenTK::Mathematics::Vector3 point, bool headshot, bool blast = false);
        // The authority's damage on another player, as an observer receives it.
        static void ObservedDamage(std::int32_t attacker, const Entities::PlayerEntity& victim, std::uint8_t beam,
            std::int32_t damage, bool headshot);
        // The authority's damage reached this machine's own player.
        static void Damage(std::int32_t attacker, const Entities::PlayerEntity& victim, std::uint8_t beam,
            std::int32_t damage, bool headshot);

        // Once a frame on a client (the strafe rig calls it): every player's
        // health as drawn here, and every position step the player's own
        // motion does not explain. Rows: hp (a change), hpup (a rise in the
        // same life: a bar going back up), jump (the step's departure from
        // the previous step over JumpThreshold).
        static void Watch();
        // The rig put this machine's player somewhere: not a jump.
        static void Placed(const Entities::PlayerEntity& player);
        [[nodiscard]] static std::string DescribeWatch();

    private:
        static constexpr float JumpThreshold = 0.25F;
        struct Track
        {
            bool Seen = false;
            std::uint16_t Life = 0;
            std::int32_t Health = 0;
            std::int32_t Samples = 0;
            OpenTK::Mathematics::Vector3 P1{};
            OpenTK::Mathematics::Vector3 P2{};
        };
        struct JumpStats
        {
            std::int64_t Samples = 0;
            std::int64_t Over25 = 0;
            std::int64_t Over50 = 0;
            std::int64_t Over100 = 0;
            float Worst = 0;
        };
        static std::array<Track, 8> _tracks;
        // [0] this machine's own player, [1] everybody else as drawn here.
        static std::array<JumpStats, 2> _jumps;
        inline static std::int64_t _healthRises = 0;
        inline static std::int64_t _healthChanges = 0;

        struct Body
        {
            float Dy = 0;
            float Pct = 0;
            float Angle = 0;
            float Radius = 0;
        };
        [[nodiscard]] static Body BodyOf(const Entities::PlayerEntity& victim, OpenTK::Mathematics::Vector3 point);
        static void Row(const char* event, std::int32_t shooter, const Entities::PlayerEntity& victim,
            std::int32_t beam, std::uint32_t launch, bool splash, bool headshot, std::int32_t damage,
            OpenTK::Mathematics::Vector3 point, const std::string& extra);

        inline static std::unique_ptr<std::ofstream> _writer{};
        inline static std::uint32_t _lastFlush = 0;
    };
}
