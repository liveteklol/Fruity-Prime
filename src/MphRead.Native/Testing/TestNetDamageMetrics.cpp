#include "Mods/Network/Diagnostics/NetDamageMetrics.hpp"
#include "Mods/Network/Diagnostics/NetShotDiagnostics.hpp"

#include <cstdint>
#include <iostream>

namespace
{
    bool Check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using MphRead::Mods::Network::NetDamageMetrics;

    NetDamageMetrics::ResetForRoomChange();
    NetDamageMetrics::RecordFired(0);
    NetDamageMetrics::RecordFired(-1);
    NetDamageMetrics::RecordPlayerCheck(0);
    NetDamageMetrics::RecordPlayerOverlap(0);
    NetDamageMetrics::RecordPlayerAccepted(0);
    NetDamageMetrics::RecordUnseenDamage(1, 2, 90);
    NetDamageMetrics::RecordProjectileOverlap(1, 0, 0, 100);
    NetDamageMetrics::RecordResolved(0);
    NetDamageMetrics::RecordReplayed(1);
    NetDamageMetrics::RecordBombCandidate(4.0F, 2.0F);
    NetDamageMetrics::RecordBombCandidate(7.0F, 5.0F);

    const auto& stats = NetDamageMetrics::Read();
    bool passed = true;
    passed &= Check(stats.Fired[0] == 1, "recorded shot count");
    passed &= Check(stats.PlayerChecks[0] == 1 && stats.PlayerOverlaps[0] == 1
        && stats.PlayerAccepted[0] == 1, "recorded projectile counters");
    passed &= Check(stats.PlayerOverlapsByShooter[1][0] == 1, "projectile overlap attribution");
    passed &= Check(stats.HitsTakenSeen == 1 && stats.HitsTakenSeenByBeam[2] == 1,
        "overlap matched pending impact");
    passed &= Check(stats.BombNearest == 4.0F && stats.BombRadiusSeen == 5.0F,
        "bomb extrema aggregation");

    NetDamageMetrics::ForgetSlot(0);
    NetDamageMetrics::ResetReplayCount(1);
    const auto& reset = NetDamageMetrics::Read();
    passed &= Check(reset.Resolved[0] == 0 && reset.Replayed[0] == 0,
        "slot diagnostic reset");
    passed &= Check(reset.Replayed[1] == 0, "replay counter reset");

    NetDamageMetrics::ResetForRoomChange();
    const auto& room = NetDamageMetrics::Read();
    passed &= Check(room.Fired[0] == 0 && room.PlayerChecks[0] == 0
        && room.BombNearest > 1.0e30F, "room metric reset");
    passed &= Check(room.HitsTaken == 0 && room.UnseenFrame[1][0] == 0,
        "initial accumulated and pending damage metrics");

    using MphRead::BeamType;
    using MphRead::Mods::Network::NetShotDiagnostics;
    NetShotDiagnostics::Reset();
    const auto weapon = static_cast<std::size_t>(NetShotDiagnostics::Bucket(BeamType::Imperialist));
    NetShotDiagnostics::RecordClaim(BeamType::Imperialist);
    NetShotDiagnostics::RecordAuthorityHit(BeamType::Imperialist, 31, true);
    NetShotDiagnostics::RecordRewind(BeamType::Imperialist, 12, true);
    const auto& shots = NetShotDiagnostics::Read();
    passed &= Check(shots.Claims[weapon] == 1 && shots.AuthorityHits[weapon] == 1
        && shots.AuthorityDamage[weapon] == 31 && shots.AuthorityHeadshots[weapon] == 1,
        "shot statistics update through owner APIs");
    passed &= Check(shots.RewindSamples[weapon] == 1 && shots.RewindFrames[weapon] == 12
        && shots.RewindClamps[weapon] == 1, "rewind metrics aggregate through owner API");
    NetShotDiagnostics::Reset();
    passed &= Check(NetShotDiagnostics::Read().Claims[weapon] == 0
        && NetShotDiagnostics::Read().RewindSamples[weapon] == 0, "shot diagnostics reset");

    std::cout << (passed ? "PASS" : "FAIL") << ": NetDamage metrics\n";
    return passed ? 0 : 1;
}
