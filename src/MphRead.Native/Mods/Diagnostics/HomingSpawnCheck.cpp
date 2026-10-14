#include "Mods/Diagnostics/HomingSpawnCheck.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/EnemySpawnEntity.hpp"
#include "Entities/Enemies/24_Gorea1A.hpp"
#include "Entities/Enemies/23_PsychoBit.hpp"
#include "Entities/PlatformEntity.hpp"
#include "Entities/PlayerSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Entity.hpp"
#include "Formats/EntityEnemy.hpp"
#include "Formats/Formats.hpp"
#include "Metadata/Metadata.hpp"
#include "Metadata/Rooms.hpp"
#include "Mods/Diagnostics/HomingCheckScene.hpp"
#include "Mods/Diagnostics/HomingReferenceFlight.hpp"
#include "Mods/Network/Diagnostics/HomingShotTrace.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"
#include "Read.hpp"
#include "Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::BeamFlags;
    using Entities::BeamProjectileEntity;
    using Entities::PlayerEntity;
    using Network::HomingShotRecord;
    using Network::HomingShotTrace;
    using OpenTK::Mathematics::Vector3;

    // Configures the live AI's equipment for a row without a placement in
    // the installed assets. The AI still owns spawning and its charge level.
    class HomingSpawnFixture final
    {
    public:
        static void WeaponRow(Entities::Enemies::Enemy23Entity& enemy, std::int32_t row)
        {
            Runtime::RequireReference(enemy._equipInfo).Weapon = Weapons::EnemyWeapons->at(row);
        }
    };

    namespace
    {
        // The enemies whose classes fire from the enemy, boss or Gorea
        // weapon tables, and the platform weapon ids.
        const std::set<EnemyType> WeaponUsers{
            EnemyType::BarbedWarWasp, EnemyType::AlimbicTurret, EnemyType::Cretaphid,
            EnemyType::PsychoBit1, EnemyType::PsychoBit2, EnemyType::Gorea1A, EnemyType::Voldrum1,
            EnemyType::Voldrum2, EnemyType::FireSpawn, EnemyType::Slench, EnemyType::SlenchTurret,
        };

        struct Site final
        {
            std::string Room{};
            std::string What{};
            Vector3 Position{};
            Vector3 Facing{};
            bool Boss = false;
            // Gorea's weapon for the run: it moves on only as a phase ends.
            std::int32_t GoreaWeapon = 0;
            std::int32_t ControlledRow = -1;
        };

        struct Census final
        {
            std::map<std::string, std::vector<Site>> ByUser{};
            std::int32_t Rooms = 0;
            std::int32_t Spawners = 0;
            std::int32_t Platforms = 0;
        };

        [[nodiscard]] Census TakeCensus()
        {
            Census census{};
            for (const std::shared_ptr<RoomMetadata>& meta : Metadata::RoomList)
            {
                if (meta == nullptr || meta->Multiplayer || meta->FirstHunt || !meta->EntityPath.has_value())
                {
                    continue;
                }
                std::shared_ptr<const std::vector<std::shared_ptr<Entity>>> entities{};
                try
                {
                    entities = Read::GetEntities(*meta->EntityPath, -1, false);
                }
                catch (...)
                {
                    continue;
                }
                ++census.Rooms;
                for (const std::shared_ptr<Entity>& entity : *entities)
                {
                    if (entity->Type == EntityType::EnemySpawn)
                    {
                        ++census.Spawners;
                        const auto& spawn = static_cast<const EntityOf<EnemySpawnEntityData>&>(*entity).Data;
                        if (WeaponUsers.contains(spawn.EnemyType))
                        {
                            const bool boss = spawn.EnemyType == EnemyType::Cretaphid
                                || spawn.EnemyType == EnemyType::Slench || spawn.EnemyType == EnemyType::Gorea1A;
                            std::string name = ::MphRead::ToString(spawn.EnemyType);
                            if (spawn.EnemyType == EnemyType::PsychoBit1 || spawn.EnemyType == EnemyType::PsychoBit2
                                || spawn.EnemyType == EnemyType::AlimbicTurret || spawn.EnemyType == EnemyType::FireSpawn)
                            {
                                name += " row " + std::to_string(spawn.Fields.S06().EnemyVersion);
                            }
                            else if (spawn.EnemyType == EnemyType::BarbedWarWasp)
                            {
                                name += " row " + std::to_string(spawn.Fields.S08().EnemyVersion);
                            }
                            census.ByUser[name].push_back(
                                Site{meta->Name, name, entity->Position, entity->FacingVector, boss});
                            if (spawn.EnemyType == EnemyType::PsychoBit1 && spawn.Fields.S06().EnemyVersion == 0
                                && meta->Name != "Level TestLevel")
                            {
                                const std::string controlled = "PsychoBit1 controlled row 8";
                                census.ByUser[controlled].push_back(
                                    Site{meta->Name, controlled, entity->Position, entity->FacingVector, false, 0, 8});
                            }
                            if (spawn.EnemyType == EnemyType::Gorea1A)
                            {
                                // Its Volt Driver and its Shock Coil, too.
                                for (const std::int32_t weapon : {1, 5})
                                {
                                    const std::string variant = name + " weapon " + std::to_string(weapon);
                                    census.ByUser[variant].push_back(Site{meta->Name, variant, entity->Position,
                                        entity->FacingVector, boss, weapon});
                                }
                            }
                        }
                    }
                    else if (entity->Type == EntityType::Platform)
                    {
                        const auto& platform = static_cast<const EntityOf<PlatformEntityData>&>(*entity).Data;
                        if (platform.BeamId >= 0 && ::MphRead::TestFlag(platform.Flags, Entities::PlatformFlags::BeamSpawner))
                        {
                            ++census.Platforms;
                            census.ByUser["Platform:" + std::to_string(platform.BeamId)].push_back(
                                Site{meta->Name, "Platform:" + std::to_string(platform.BeamId), entity->Position,
                                    entity->FacingVector, false});
                        }
                    }
                }
            }
            return census;
        }

        [[nodiscard]] const WeaponInfo* RowOf(const std::string& row)
        {
            const std::size_t colon = row.find(':');
            if (colon == std::string::npos)
            {
                return nullptr;
            }
            const std::string table = row.substr(0, colon);
            const std::size_t index = static_cast<std::size_t>(std::stoi(row.substr(colon + 1)));
            const std::array<std::pair<const char*, const std::shared_ptr<const Weapons::WeaponList>*>, 6> tables{{
                {"1P", &Weapons::Weapons1P}, {"MP", &Weapons::WeaponsMP}, {"Enemy", &Weapons::EnemyWeapons},
                {"Boss", &Weapons::BossWeapons}, {"Gorea", &Weapons::GoreaWeapons},
                {"Platform", &Weapons::PlatformWeapons},
            }};
            for (const auto& [name, list] : tables)
            {
                if (table == name && *list != nullptr && index < (*list)->size())
                {
                    return (**list)[index].get();
                }
            }
            return nullptr;
        }

        struct Flight final
        {
            HomingShotRecord Record{};
            std::shared_ptr<BeamProjectileEntity> Beam{};
            Vector3 StartVelocity{};
            float TurnDegrees = 0.0F;
            bool HeldTarget = false;
            bool Done = false;
        };

        struct Tally final
        {
            std::int32_t Shots = 0;
            std::int32_t Homing = 0;
            std::int32_t Locked = 0;
            std::int32_t Turned = 0;
            std::int32_t TableMismatches = 0;
            std::set<std::uint16_t> Charges{};
            float MaxTurn = 0.0F;
            float HomingRaw = 0.0F;
        };

        [[nodiscard]] float AngleDegrees(Vector3 a, Vector3 b)
        {
            const float lengths = std::sqrt(Vector3::Dot(a, a) * Vector3::Dot(b, b));
            return lengths > 0.0F
                ? std::acos(std::clamp(Vector3::Dot(a, b) / lengths, -1.0F, 1.0F)) * 57.29578F : 0.0F;
        }

        [[nodiscard]] Vector3 StandingPoint(Scene& scene, const Site& site)
        {
            if (site.Boss)
            {
                auto spawns = scene.GetPlayerSpawnEntities().GetEnumerator();
                if (spawns.MoveNext())
                {
                    return static_cast<Vector3>(spawns.Current()->Position) + Vector3(0.0F, 0.5F, 0.0F);
                }
            }
            Vector3 facing(site.Facing.X, 0.0F, site.Facing.Z);
            const float length = std::sqrt(Vector3::Dot(facing, facing));
            facing = length > 0.0F ? ::OpenTK::Mathematics::Scale(facing, 1.0F / length) : Vector3(0.0F, 0.0F, 1.0F);
            return site.Position + ::OpenTK::Mathematics::Scale(facing, 7.0F) + Vector3(0.0F, 0.5F, 0.0F);
        }

        // One site, live: its room loaded, its spawner or platform switched
        // on, the player held in front of it for `seconds`.
        bool Live(const Site& site, std::int32_t seconds, std::map<std::string, Tally>& tallies,
            std::map<std::string, std::string>& notes)
        {
            std::unique_ptr<HomingCheckScene> loaded{};
            try
            {
                loaded = std::make_unique<HomingCheckScene>(site.Room, true);
            }
            catch (...)
            {
                notes[site.What + " in " + site.Room] = "room not usable here: "
                    + Runtime::ExceptionToString(std::current_exception());
                return false;
            }
            HomingCheckScene& fixture = *loaded;
            Scene& scene = fixture.Get();
            auto spawners = scene.GetEnemySpawnEntities().GetEnumerator();
            while (spawners.MoveNext())
            {
                const auto& spawner = spawners.Current();
                spawner->SetActive(true);
                spawner->Flags &= ~Entities::SpawnerFlags::Suspended;
            }
            auto platforms = scene.GetPlatformEntities().GetEnumerator();
            while (platforms.MoveNext())
            {
                platforms.Current()->SetActive(true);
            }
            PlayerEntity& player = fixture.Shooter();
            const Vector3 stand = StandingPoint(scene, site);
            std::vector<Flight> flights{};
            HomingShotTrace::SetSpawnSink([&flights](const HomingShotRecord& record, const BeamProjectileEntity& beam)
            {
                if (record.OwnerType != EntityType::EnemyInstance && record.OwnerType != EntityType::Platform)
                {
                    return;
                }
                Flight flight{};
                flight.Record = record;
                flight.Beam = std::static_pointer_cast<BeamProjectileEntity>(
                    const_cast<BeamProjectileEntity&>(beam).shared_from_this());
                flight.StartVelocity = beam.Velocity();
                flight.HeldTarget = beam.Target() != nullptr;
                flights.push_back(std::move(flight));
            });
            bool goreaSet = site.GoreaWeapon == 0;
            try
            {
                for (std::int32_t frame = 0; frame < seconds * 60; ++frame)
                {
                    if (site.ControlledRow >= 0)
                    {
                        auto enemies = scene.GetEnemyInstanceEntities().GetEnumerator();
                        while (enemies.MoveNext())
                        {
                            const auto& enemy = enemies.Current();
                            if (enemy->EnemyType() == EnemyType::PsychoBit1)
                            {
                                HomingSpawnFixture::WeaponRow(
                                    static_cast<Entities::Enemies::Enemy23Entity&>(*enemy), site.ControlledRow);
                            }
                        }
                    }
                    if (!goreaSet)
                    {
                        auto enemies = scene.GetEnemyInstanceEntities().GetEnumerator();
                        while (enemies.MoveNext())
                        {
                            const auto& enemy = enemies.Current();
                            if (enemy->EnemyType() == EnemyType::Gorea1A)
                            {
                                auto& gorea = static_cast<Entities::Enemies::Enemy24Entity&>(*enemy);
                                for (std::int32_t i = 0; i < site.GoreaWeapon; ++i)
                                {
                                    gorea.ModChangeWeapon();
                                }
                                goreaSet = true;
                            }
                        }
                    }
                    player.SetHealth(99);
                    player.ModPlaceAt(stand);
                    fixture.Step();
                    for (Flight& flight : flights)
                    {
                        if (flight.Done)
                        {
                            continue;
                        }
                        const BeamProjectileEntity& beam = *flight.Beam;
                        if (beam.Lifespan() <= 0.0F || ::MphRead::TestFlag(beam.Flags(), BeamFlags::Collided))
                        {
                            flight.Done = true;
                            continue;
                        }
                        flight.TurnDegrees = AngleDegrees(flight.StartVelocity, beam.Velocity());
                    }
                }
            }
            catch (...)
            {
                notes[site.What + " in " + site.Room] = "stopped: " + Runtime::ExceptionToString(std::current_exception());
            }
            HomingShotTrace::SetSpawnSink({});
            for (const Flight& flight : flights)
            {
                const HomingShotRecord& record = flight.Record;
                Tally& tally = tallies["near " + site.What + ": " + record.WeaponRow];
                ++tally.Shots;
                tally.Charges.insert(record.Charge);
                tally.HomingRaw = std::max(tally.HomingRaw, record.HomingRaw);
                if (const WeaponInfo* row = RowOf(record.WeaponRow); row != nullptr)
                {
                    const float expected = HomingReferenceFlight::FromWeapon(*row, record.Charge).HomingRaw;
                    if (std::abs(expected - record.HomingRaw) > 0.01F)
                    {
                        ++tally.TableMismatches;
                    }
                }
                if (record.Homing)
                {
                    ++tally.Homing;
                    tally.Locked += record.Target.Kind != Network::IntentPacket::ShotTargetKind::None ? 1 : 0;
                    if (!record.Continuous && flight.TurnDegrees > 0.5F)
                    {
                        ++tally.Turned;
                    }
                    tally.MaxTurn = std::max(tally.MaxTurn, flight.TurnDegrees);
                }
            }
            if (flights.empty() && !notes.contains(site.What + " in " + site.Room))
            {
                notes[site.What + " in " + site.Room] = "fired nothing in " + std::to_string(seconds) + " s";
            }
            return true;
        }
    }

    std::int32_t HomingSpawnCheck::Run(std::int32_t seconds)
    {
        std::int32_t checks = 0;
        std::int32_t failures = 0;
        const auto check = [&](bool ok, const std::string& name)
        {
            ++checks;
            failures += ok ? 0 : 1;
            Runtime::ConsoleWriteLine(std::string(ok ? "SPAWN PASS " : "SPAWN FAIL ") + name);
        };
        try
        {
            const Census census = TakeCensus();
            Runtime::ConsoleWriteLine("SPAWN census: " + std::to_string(census.Rooms) + " story rooms, "
                + std::to_string(census.Spawners) + " enemy spawners, " + std::to_string(census.Platforms)
                + " weapon platforms");
            for (const auto& [user, sites] : census.ByUser)
            {
                std::set<std::string> rooms{};
                for (const Site& site : sites)
                {
                    rooms.insert(site.Room);
                }
                std::ostringstream line;
                line << "SPAWN census " << user << ": " << sites.size() << " in " << rooms.size() << " room(s):";
                for (const std::string& room : rooms)
                {
                    line << ' ' << room;
                }
                Runtime::ConsoleWriteLine(line.str());
            }

            std::map<std::string, Tally> tallies{};
            std::map<std::string, std::string> notes{};
            for (const auto& [user, sites] : census.ByUser)
            {
                // Every room of each: which weapon version a spawner gives its
                // enemy is in that room's data alone.
                for (const Site& site : sites)
                {
                    Runtime::ConsoleWriteLine("SPAWN live " + user + " in " + site.Room);
                    static_cast<void>(Live(site, seconds, tallies, notes));
                }
            }

            for (const auto& [name, tally] : tallies)
            {
                std::ostringstream line;
                line << "SPAWN fired " << name << ": " << tally.Shots << " shot(s), charges";
                for (const std::uint16_t charge : tally.Charges)
                {
                    line << ' ' << charge;
                }
                line << " | homing " << Runtime::ToString(tally.HomingRaw, "F1") << ", " << tally.Homing
                     << " steering, " << tally.Locked << " locked, " << tally.Turned
                     << " turned (max " << Runtime::ToString(tally.MaxTurn, "F1") << " deg)"
                     << " | table mismatches " << tally.TableMismatches;
                Runtime::ConsoleWriteLine(line.str());
                check(tally.TableMismatches == 0,
                    name + ": every shot's steering is the table's value at the charge it was spawned with");
                if (tally.Homing > 0)
                {
                    check(tally.Locked > 0, name + ": a homing shot fired at the player locks onto the player");
                }
            }
            for (const auto& [site, note] : notes)
            {
                Runtime::ConsoleWriteLine("SPAWN note " + site + ": " + note);
            }
            for (const std::string row : {"Enemy:0", "Enemy:2", "Enemy:8", "Enemy:9", "Enemy:10",
                    "Boss:2", "Boss:3", "Gorea:1", "Platform:3"})
            {
                const bool observed = std::any_of(tallies.begin(), tallies.end(), [&](const auto& item)
                {
                    return item.first.ends_with(": " + row) && item.second.Shots > 0;
                });
                check(observed, row + ": required weapon fired from a live enemy, boss or platform");
            }
        }
        catch (...)
        {
            Runtime::ConsoleWriteLine("SPAWN FAIL " + Runtime::ExceptionToString(std::current_exception()));
            return 1;
        }
        Runtime::ConsoleWriteLine("SPAWN " + std::string(failures == 0 ? "PASS " : "FAIL ")
            + std::to_string(checks - failures) + "/" + std::to_string(checks) + " checks");
        return failures == 0 ? 0 : 1;
    }
}
