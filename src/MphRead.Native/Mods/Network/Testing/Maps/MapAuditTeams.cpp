#include "Mods/Network/Testing/Maps/MapAudit.hpp"

#include "Mods/ScreenCapture.hpp"
#include "Entities/FlagBaseEntity.hpp"
#include "Entities/NodeDefenseEntity.hpp"
#include "Entities/OctolithFlagEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::PlayerEntity;
    using OpenTK::Mathematics::Vector3;

    namespace
    {
        [[nodiscard]] PlayerEntity& PlayerAt(std::int32_t slot)
        {
            return Runtime::RequireReference(PlayerEntity::Players().at(static_cast<std::size_t>(slot)));
        }
    }

    void MapAudit::CaptureTeamResults()
    {
        if (!_shotDirectory.has_value() || !ShowWindow() || GameState::Mode() != GameMode::BattleTeams)
        {
            return;
        }
        GameState::MatchState(MphRead::MatchState::Ending);
        PlayerEntity::SetModForceScoreboard(true);
        for (const bool tie : {false, true})
        {
            for (std::int32_t slot = 0; slot < 8; slot++)
            {
                GameState::Points()[slot] = tie ? 2 : PlayerAt(slot).TeamIndex() == 3 ? 4 : 2;
                GameState::Kills()[slot] = GameState::Deaths()[slot] = 0;
            }
            GameState::UpdateState();
            _scene->OnDrawFrame();
            static_cast<void>(_scene->OnRenderFrame());
            static_cast<void>(Mods::ScreenCapture::SaveWindow(_scene.get(),
                Runtime::PathCombine(*_shotDirectory, tie ? "teams-tie.png" : "teams-winner.png")));
        }
    }

    std::vector<std::string> MapAudit::RunTeamProbe()
    {
        std::vector<std::string> failures;
        std::int32_t checks = 0;
        const auto check = [&](bool result, const std::string& message)
        {
            Runtime::IncrementInPlace(checks);
            if (!result)
            {
                failures.push_back(message);
                std::cout << "MAPFAIL " << _room << " | teamprobe: " << message << '\n';
            }
        };
        try
        {
            constexpr std::array<std::int32_t, 8> expected{0, 1, 2, 3, 3, 2, 1, 0};
            check(GameState::TeamCount() == 4 && GameState::Teams(), "four-team match configured");
            for (std::int32_t slot = 0; slot < 8; slot++)
            {
                PlayerEntity& player = PlayerAt(slot);
                check(player.TeamIndex() == expected[static_cast<std::size_t>(slot)]
                    && _everSpawned[static_cast<std::size_t>(slot)],
                    "slot " + std::to_string(slot) + " team/spawn");
                player.SetHealth(99);
                player.SetIsBot(false);
                player.ModForceForm(false);
                GameState::Points()[slot] = GameState::Deaths()[slot] = GameState::Kills()[slot] = 0;
                GameState::TeamDeaths()[slot] = 0;
            }
            GameState::PointGoal(999);
            GameState::FriendlyFire(false);
            PlayerEntity& target = PlayerAt(7);
            target.TakeDamage(5U, Entities::DamageFlags::IgnoreInvuln, std::nullopt, &PlayerAt(0));
            check(target.Health() == 99, "slot 0 cannot damage allied slot 7");
            target.TakeDamage(5U, Entities::DamageFlags::IgnoreInvuln, std::nullopt, &PlayerAt(3));
            check(target.Health() < 99, "team D can damage team A");
            const std::int32_t beforeFriendly = target.Health();
            GameState::FriendlyFire(true);
            target.TakeDamage(5U, Entities::DamageFlags::IgnoreInvuln, std::nullopt, &PlayerAt(0));
            check(target.Health() < beforeFriendly, "friendly fire rule enables allied damage");
            GameState::FriendlyFire(false);

            GameState::Points()[7] = 4;
            GameState::Points()[4] = 5;
            GameState::UpdateState();
            check(GameState::TeamPoints()[0] == 4 && GameState::TeamPoints()[3] == 5,
                "upper slots contribute to assigned team totals");
            const GameMode mode = GameState::Mode();
            if (mode == GameMode::BattleTeams || mode == GameMode::BountyTeams || mode == GameMode::NodesTeams)
            {
                check(GameState::Standings()[4] == 0 && GameState::Standings()[7] == 1, "team D wins and team A is second");
            }
            if (mode == GameMode::SurvivalTeams)
            {
                GameState::PointGoal(2);
                GameState::MatchTime(60);
                for (std::int32_t slot = 0; slot < 8; slot++)
                {
                    PlayerAt(slot).SetHealth(expected[static_cast<std::size_t>(slot)] == 0 ? 0 : 99);
                    GameState::TeamDeaths()[slot] = 3;
                }
                GameState::UpdateSurvival(_scene->FrameTime());
                check(GameState::MatchTime() == 60, "eliminating A leaves three competing teams");
                for (std::int32_t slot = 0; slot < 8; slot++)
                {
                    PlayerAt(slot).SetHealth(expected[static_cast<std::size_t>(slot)] == 3 ? 99 : 0);
                }
                GameState::UpdateSurvival(_scene->FrameTime());
                GameState::UpdateState();
                check(GameState::MatchTime() == 0 && GameState::Standings()[4] == 0 && GameState::TeamTime()[3] == -1,
                    "surviving team D wins with MAX time");
            }
            else if (mode == GameMode::NodesTeams || mode == GameMode::DefenderTeams)
            {
                std::shared_ptr<Entities::NodeDefenseEntity> node{};
                for (auto enumerator = _scene->GetNodeDefenseEntities().GetEnumerator(); enumerator.MoveNext();)
                {
                    node = enumerator.Current();
                    break;
                }
                check(node != nullptr, "objective exists in selected map layer");
                if (node != nullptr)
                {
                    const auto occupy = [&node](std::int32_t slot)
                    {
                        for (const std::shared_ptr<PlayerEntity>& player : PlayerEntity::Players())
                        {
                            Runtime::RequireReference(player).SetHealth(0);
                        }
                        PlayerEntity& occupant = PlayerAt(slot);
                        occupant.SetHealth(99);
                        const Vector3 center = VolumeCenter(node->Volume());
                        occupant.Reposition(center - occupant.Volume().SpherePosition, occupant.NodeRef);
                    };
                    if (mode == GameMode::DefenderTeams)
                    {
                        occupy(7);
                        float before = GameState::TeamTime()[0];
                        static_cast<void>(node->Process());
                        check(node->CurrentTeam() == 0 && GameState::TeamTime()[0] > before,
                            "slot 7 contributes Defender time to A");
                        occupy(4);
                        before = GameState::TeamTime()[3];
                        static_cast<void>(node->Process());
                        check(node->CurrentTeam() == 3 && GameState::TeamTime()[3] > before,
                            "slot 4 contributes Defender time to D");
                        PlayerEntity& other = PlayerAt(7);
                        other.SetHealth(99);
                        other.Reposition(VolumeCenter(node->Volume()) - other.Volume().SpherePosition, other.NodeRef);
                        static_cast<void>(node->Process());
                        check(node->Contested() && node->CurrentTeam() == Entities::NodeDefenseEntity::NoTeam,
                            "different teams contest Defender without a false owner");
                    }
                    else
                    {
                        const auto steps = static_cast<std::int32_t>(std::ceil(11 / std::max(_scene->FrameTime(), 1 / 60.0F)));
                        for (const std::int32_t slot : {4, 7, 4})
                        {
                            occupy(slot);
                            for (std::int32_t frame = 0; frame < steps; frame++)
                            {
                                static_cast<void>(node->Process());
                            }
                            check(node->CurrentTeam() == expected[static_cast<std::size_t>(slot)],
                                "slot " + std::to_string(slot) + " captures Nodes for assigned team");
                        }
                        check(GameState::NodesCaptured()[7] > 0 && GameState::NodesCaptured()[4] > 0,
                            "upper-slot Node capture accounting");
                        PlayerAt(4).SetHealth(0);
                        static_cast<void>(node->Process());
                        check(!node->IsOccupied() && node->OccupyingTeam() == Entities::NodeDefenseEntity::NoTeam,
                            "upper-slot occupancy clears after departure");
                    }
                }
            }
            else if (mode == GameMode::BountyTeams)
            {
                std::shared_ptr<Entities::OctolithFlagEntity> flag{};
                std::shared_ptr<Entities::FlagBaseEntity> flagBase{};
                for (auto enumerator = _scene->GetOctolithFlagEntities().GetEnumerator(); enumerator.MoveNext();)
                {
                    flag = enumerator.Current();
                    break;
                }
                for (auto enumerator = _scene->GetFlagBaseEntities().GetEnumerator(); enumerator.MoveNext();)
                {
                    flagBase = enumerator.Current();
                    break;
                }
                check(flag != nullptr && flagBase != nullptr, "Bounty flag and base exist");
                if (flag != nullptr && flagBase != nullptr)
                {
                    if (flag->Carrier() != nullptr)
                    {
                        flag->OnCaptured();
                    }
                    for (const std::shared_ptr<PlayerEntity>& player : PlayerEntity::Players())
                    {
                        Runtime::RequireReference(player).SetHealth(0);
                    }
                    const std::shared_ptr<PlayerEntity> carrier = PlayerEntity::Players().at(4);
                    carrier->SetHealth(99);
                    const float maxPickup = Fixed::ToFloat(carrier->Values().MaxPickupHeight);
                    const float minPickup = Fixed::ToFloat(carrier->Values().MinPickupHeight);
                    const Vector3 flagPosition = flag->Position;
                    const Vector3 pickupCenter(flagPosition.X,
                        flagPosition.Y + (-maxPickup - 1.25F + (maxPickup - minPickup + 0.5F) / 2), flagPosition.Z);
                    carrier->ModForceForm(true);
                    carrier->ModForceForm(false);
                    carrier->Reposition(pickupCenter - static_cast<Vector3>(carrier->Position), carrier->NodeRef);
                    carrier->SetPrevPosition(static_cast<Vector3>(carrier->Position) - Vector3(0.1F, 0.0F, 0.0F));
                    static_cast<void>(flag->Process());
                    check(flag->Carrier() == carrier, "slot 4 picks up Bounty flag");
                    if (flag->Carrier() == carrier)
                    {
                        const std::int32_t before = GameState::Points()[4];
                        const Vector3 center = VolumeCenter(CollisionVolume::Move(flagBase->Data().Volume, flagBase->Position));
                        carrier->Reposition(center - static_cast<Vector3>(carrier->Position), carrier->NodeRef);
                        static_cast<void>(flagBase->Process());
                        GameState::UpdateState();
                        check(GameState::Points()[4] == before + 1 && GameState::TeamPoints()[3] == before + 1,
                            "Bounty delivery scores for team D");
                    }
                }
            }
        }
        catch (...)
        {
            const std::exception_ptr exception = std::current_exception();
            failures.push_back(Runtime::ExceptionMessage(exception));
            std::cout << "MAPFAIL " << _room << " | teamprobe: "
                << Runtime::ExceptionToString(exception) << '\n';
        }
        std::cout << "TEAMPROBE " << _room << " | " << ::MphRead::ToString(GameState::Mode()) << " | " << checks
            << " checks | " << failures.size() << " failures\n";
        return failures;
    }
}
