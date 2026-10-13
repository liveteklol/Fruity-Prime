#include "../Mods/Combat/SyluxMuzzleGuard.hpp"
#include "Mods/Network/Gameplay/Weapons/MuzzleObstructionHistory.hpp"
#include "../Metadata/Player.hpp"
#include "../Entities/Players/PlayerEntity.hpp"

#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace OpenTK::Mathematics;
using MphRead::Hunter;
using MphRead::BeamType;
using MphRead::Mods::Combat::SyluxMuzzleGuard;
using MphRead::Mods::Combat::BeamObstacleHit;
using MphRead::Mods::Network::MuzzleObstructionHistory;
using MphRead::Mods::Network::ShotKey;

namespace
{
    int checks = 0;
    void Expect(bool condition, const char* name)
    {
        if (!condition) throw std::runtime_error(name);
        ++checks;
    }
    bool Near(Vector3 a, Vector3 b) { return LengthSquared(a - b) < 1e-10F; }
}

int main()
{
    try
    {
        static_assert(SyluxMuzzleGuard::Length == 431.0F / 4096.0F);
        const auto& sylux = MphRead::Metadata::PlayerValues.at(static_cast<std::size_t>(Hunter::Sylux));
        const auto& noxus = MphRead::Metadata::PlayerValues.at(static_cast<std::size_t>(Hunter::Noxus));
        Expect(sylux.FieldB0 == noxus.FieldB0 && sylux.FieldB4 == noxus.FieldB4 && sylux.FieldB8 == noxus.FieldB8,
            "Sylux and Noxus retain matching gun placement metadata");
        Expect(sylux.MuzzleOffset == 0x800 && noxus.MuzzleOffset == 0x651,
            "guard constants match immutable original muzzle offsets");
        const Vector3 gun(2, -3, 4);
        for (Vector3 aim : {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, -1),
            Normalize(Vector3(1, 2, -3))})
        {
            const Vector3 muzzle = gun + ScaleVector(aim, 0.5F);
            for (Vector3 shift : {Vector3{}, Vector3(-13, 5, 7)})
            {
                const auto start = SyluxMuzzleGuard::Start(Hunter::Sylux, gun, aim, muzzle, muzzle + shift);
                Expect(start && Near(*start, gun + ScaleVector(aim, 1617.0F / 4096.0F) + shift), "short gun-axis start");
                Expect(Near(muzzle + shift - *start, ScaleVector(aim, 431.0F / 4096.0F)), "remote shift preserves axis and length");
                Expect(SyluxMuzzleGuard::ValidSegment(*start, muzzle + shift), "valid horizontal vertical diagonal segment");
                for (int hunter = 0; hunter < 8; ++hunter)
                {
                    if (static_cast<Hunter>(hunter) == Hunter::Sylux) continue;
                    Expect(!SyluxMuzzleGuard::Start(static_cast<Hunter>(hunter), gun, aim, muzzle, muzzle + shift),
                        "other seven hunters never opt in");
                }
            }
        }
        Expect(!SyluxMuzzleGuard::ValidSegment(gun, gun), "zero axis fails closed");
        Expect(!SyluxMuzzleGuard::ValidSegment(gun, gun + Vector3(1, 0, 0)), "long external segment rejected");
        Expect(!SyluxMuzzleGuard::ValidSegment({std::numeric_limits<float>::quiet_NaN(), 0, 0}, gun), "NaN rejected");
        Expect(!SyluxMuzzleGuard::ValidSegment(gun, {std::numeric_limits<float>::infinity(), 0, 0}), "infinity rejected");
        Expect(!SyluxMuzzleGuard::ValidSegment({1e20F, 0, 0}, {1e20F, 1, 0}), "unindexable world coordinates rejected");

        MphRead::Formats::CollisionResult geometry{};
        const Vector3 front(0.45F, 0, 0), back(0.35F, 0, 0), x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
        Expect(MphRead::Mods::Combat::IntersectBeamDoor(front, back, false, false, x, {}, 1, 2, geometry),
            "closed active door intersects its existing 0.4 plane");
        const float tie = geometry.Distance;
        Expect(!MphRead::Mods::Combat::IntersectBeamDoor(front, back, true, false, x, {}, 1, 2, geometry)
            && !MphRead::Mods::Combat::IntersectBeamDoor(front, back, false, true, x, {}, 1, 2, geometry),
            "open or disconnected door is ignored");
        Expect(MphRead::Mods::Combat::IntersectBeamDoor(-front, -back, false, false, x, {}, 1, 2, geometry)
            && geometry.Plane.X == -1, "door collision faces either approach");
        Expect(!MphRead::Mods::Combat::IntersectBeamDoor(front + ScaleVector(y, 2), back + ScaleVector(y, 2), false, false, x, {}, 1, 2, geometry),
            "door radius excludes outside geometry");
        Expect(!MphRead::Mods::Combat::IntersectBeamDoor(front, back, false, false, x, {}, 1, tie, geometry),
            "equal distance preserves earlier map or door candidate");
        const Vector4 fieldPlane(1, 0, 0, 0.4F);
        Expect(MphRead::Mods::Combat::IntersectBeamForceField(front, back, true, fieldPlane, {}, y, z, 1, 1, 2, geometry),
            "active forcefield uses the same finite plane intersection");
        Expect(!MphRead::Mods::Combat::IntersectBeamForceField(front, back, false, fieldPlane, {}, y, z, 1, 1, 2, geometry),
            "inactive forcefield is ignored");
        Expect(!MphRead::Mods::Combat::IntersectBeamForceField(front + ScaleVector(y, 2), back + ScaleVector(y, 2), true, fieldPlane, {}, y, z, 1, 1, 2, geometry)
            && !MphRead::Mods::Combat::IntersectBeamForceField(front + ScaleVector(z, 2), back + ScaleVector(z, 2), true, fieldPlane, {}, y, z, 1, 1, 2, geometry),
            "forcefield width and height exclude outside geometry");
        Expect(MphRead::Mods::Combat::IntersectBeamForceField(front + y + z, back + y + z, true, fieldPlane, {}, y, z, 1, 1, 2, geometry),
            "forcefield width and height boundaries remain inclusive");
        Expect(!MphRead::Mods::Combat::IntersectBeamForceField(front, back, true, fieldPlane, {}, y, z, 1, 1, tie, geometry),
            "map door and forcefield keep strict nearest-hit priority");

        MuzzleObstructionHistory history;
        ShotKey key(9, 2, 3, 4, 5, 100);
        BeamObstacleHit hit{};
        Expect(!history.Contains(key, BeamType::PowerBeam, 100), "open shots have no history");
        history.Record(key, BeamType::PowerBeam, hit, 101);
        Expect(history.Contains(key, BeamType::PowerBeam, 102), "blocked shot stored");
        Expect(!history.Contains(key, BeamType::Missile, 102), "weapon identity isolated");
        history.RecordDescendant(key, BeamType::Judicator, 102);
        Expect(history.Contains(key, BeamType::Judicator, 103), "ricochet's beam kind requires authority proof too");
        history.Record(key, BeamType::Missile, hit, 102);
        Expect(history.Contains(key, BeamType::Missile, 103) && history.Contains(key, BeamType::PowerBeam, 103),
            "same-frame multi-weapon and pellets retain blocked identity");
        for (int field = 0; field < 6; ++field)
        {
            auto other = key;
            if (field == 0) ++other.AuthorityEpoch;
            if (field == 1) ++other.MatchId;
            if (field == 2) ++other.ShooterSlot;
            if (field == 3) ++other.Generation;
            if (field == 4) ++other.LifeId;
            if (field == 5) ++other.LaunchFrame;
            Expect(!history.Contains(other, BeamType::PowerBeam, 103), "epoch match slot generation life frame isolation");
        }
        Expect(!history.Contains(key, BeamType::PowerBeam, 102 + MuzzleObstructionHistory::Depth), "history expires");
        history.ForgetSlot(2);
        Expect(history.Contains(key, BeamType::PowerBeam, 103), "other disconnect retains record");
        history.ForgetSlot(3);
        Expect(!history.Contains(key, BeamType::PowerBeam, 103), "disconnect and respawn discard own record");
        history.Record(key, BeamType::PowerBeam, hit, 101);
        auto wrapped = key; wrapped.LaunchFrame += MuzzleObstructionHistory::Depth;
        history.Record(wrapped, BeamType::Missile, hit, 500);
        Expect(!history.Contains(key, BeamType::PowerBeam, 501) && history.Contains(wrapped, BeamType::Missile, 501),
            "ring reuse cannot alias an old launch or weapon");
        history.Reset();
        history.RecordDescendant(wrapped, BeamType::Missile, 501);
        Expect(!history.Contains(wrapped, BeamType::Missile, 501), "map and authority reset discard history");
        std::printf("PASS Sylux muzzle geometry and blocked-shot identity: %d checks\n", checks);
        return 0;
    }
    catch (const std::exception& ex) { std::fprintf(stderr, "FAIL %s\n", ex.what()); return 1; }
}
