#include "Mods/Network/Testing/Combat/HealthSimulationTest.hpp"

#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Session/NetMatchTimeSync.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Testing/Harness/SimulationFixture.hpp"
#include "Mods/Input/SyntheticInput.hpp"
#include "Mods/Multiplayer/MapResourceRules.hpp"
#include "Entities/ItemInstanceEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Number.hpp"
#include "Program.hpp"
#include "Read.hpp"
#include "Scene.hpp"

#include <exception>
#include <memory>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::ItemSpawnEntity;

    std::int32_t HealthSimulationTest::Run(const std::string& room)
    {
        ServerSim sim{};
        std::int32_t result = 1;
        try
        {
            const SessionStatePacket session = SimulationFixture::Session(room);
            const RosterPacket roster = SimulationFixture::Roster("Health");
            std::vector<std::uint8_t> latest;
            if (!sim.Start(room, GameMode::Battle, 8,
                    [&latest](std::span<const std::uint8_t> payload) { latest.assign(payload.begin(), payload.end()); },
                    []() {}, MakeNetSessionServerSimulationPort(), roster, session))
            {
                throw ProgramException("Authority could not load the room.");
            }
            for (std::int32_t i = 0; i < 180; i++)
            {
                sim.Step();
            }
            std::shared_ptr<ItemSpawnEntity> spawn{};
            for (const std::shared_ptr<ItemSpawnEntity>& candidate : NetItemState::RegisteredSpawns())
            {
                if (candidate->Item() != nullptr && candidate->Active
                    && Multiplayer::MapResourceRules::IsHealth(candidate->Data().ItemType))
                {
                    spawn = candidate;
                    break;
                }
            }
            if (spawn == nullptr)
            {
                throw System::InvalidOperationException("Sequence contains no matching element");
            }
            const std::size_t count = NetItemState::RegisteredSpawns().size();
            const auto id = static_cast<std::int16_t>(spawn->Id);
            spawn->Item()->OnPickedUp();
            for (std::int32_t i = 0; i < 3; i++)
            {
                sim.Step();
            }
            if (spawn->Item() != nullptr)
            {
                throw ProgramException("Picked health did not despawn.");
            }
            const std::vector<std::uint8_t> unavailable = Tail(latest);
            for (std::int32_t i = 0; i < 1200 && spawn->Item() == nullptr; i++)
            {
                sim.Step();
            }
            if (spawn->Item() == nullptr)
            {
                throw ProgramException("Health did not respawn.");
            }
            const std::vector<std::uint8_t> available = Tail(latest);
            if (sim.StepFailures() != 0)
            {
                throw ProgramException("Authority simulation had failed steps.");
            }
            sim.Stop();

            NetSession::StartPlayback();
            NetSession::ApplySessionState(session);
            NetSession::ApplyRoster(roster);
            Entities::PlayerEntity::SetMaxPlayers(8);
            auto keyboard = Input::SyntheticInput::CreateKeyboard();
            auto mouse = Input::SyntheticInput::CreateMouse();
            auto scene = std::make_shared<Scene>(OpenTK::Mathematics::Vector2i(256, 192), *keyboard, *mouse,
                [](std::string) {}, []() {});
            NetLaunch::BuildPlayers(*scene, Hunter::Samus, 0, false, -1);
            scene->AddRoom(room, GameMode::Battle, NetLaunch::RoomPlayerCount());
            scene->OnLoad();
            if (NetItemState::RegisteredSpawns().size() != count)
            {
                throw ProgramException("Replica constructed different item spawners.");
            }
            std::shared_ptr<ItemSpawnEntity> replica{};
            for (const std::shared_ptr<ItemSpawnEntity>& candidate : NetItemState::RegisteredSpawns())
            {
                if (candidate->Id == id)
                {
                    if (replica != nullptr)
                    {
                        throw System::InvalidOperationException("Sequence contains more than one matching element");
                    }
                    replica = candidate;
                }
            }
            if (replica == nullptr)
            {
                throw System::InvalidOperationException("Sequence contains no matching element");
            }
            NetItemState::Receive(unavailable);
            for (std::int32_t i = 0; i < 3; i++)
            {
                scene->OnSimulationFrame();
            }
            if (replica->Item() != nullptr)
            {
                throw ProgramException("Replica spawned unavailable health.");
            }
            NetItemState::Receive(available);
            for (std::int32_t i = 0; i < 3; i++)
            {
                scene->OnSimulationFrame();
            }
            if (replica->Item() == nullptr || !NetItemState::Mirrors(*replica))
            {
                throw ProgramException("Replica did not restore authoritative availability.");
            }
            NetItemState::Receive(unavailable);
            for (std::int32_t i = 0; i < 3; i++)
            {
                scene->OnSimulationFrame();
            }
            if (replica->Item() != nullptr)
            {
                throw ProgramException("Replica did not remove consumed health.");
            }
            Runtime::ConsoleWriteLine("[healthsimtest] PASS " + room + ": " + Runtime::ToString(count)
                + " item spawners; authority health pickup/respawn and replica convergence.");
            result = 0;
        }
        catch (...)
        {
            Runtime::ConsoleWriteLine("[healthsimtest] FAIL " + room + ": "
                + Runtime::ExceptionToString(std::current_exception()));
            result = 1;
        }
        sim.Stop();
        NetSession::Stop();
        Read::ClearCache();
        return result;
    }

    std::vector<std::uint8_t> HealthSimulationTest::Tail(const std::vector<std::uint8_t>& snapshot)
    {
        if (snapshot.size() < static_cast<std::size_t>(SnapshotHeader::Size))
        {
            throw ProgramException("No authoritative snapshot.");
        }
        const std::size_t start = SnapshotHeader::Size
            + static_cast<std::size_t>(SnapshotHeader::Read(snapshot).PlayerCount) * PlayerState::Size + NetMatchTimeSync::Size;
        if (snapshot.size() < start || !NetItemState::Validate(std::span<const std::uint8_t>(snapshot).subspan(start)))
        {
            throw ProgramException("Invalid health snapshot tail.");
        }
        return std::vector<std::uint8_t>(snapshot.begin() + static_cast<std::ptrdiff_t>(start), snapshot.end());
    }
}
