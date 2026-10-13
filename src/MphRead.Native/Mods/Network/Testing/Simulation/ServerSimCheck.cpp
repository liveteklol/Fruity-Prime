#include "Mods/Network/Testing/Simulation/ServerSimCheck.hpp"
#include "Mods/Network/Gameplay/Replication/FormReconciliation.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Formats/Types.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#endif

using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::TestFlag;

namespace
{
    using MphRead::GameMode;
    using MphRead::Entities::LoadFlags;
    using MphRead::Entities::PlayerEntity;
    using MphRead::Mods::Network::IntentButtons;
    using MphRead::Mods::Network::IntentPacket;
    using OpenTK::Mathematics::Vector3;

    template <typename T>
    [[nodiscard]] std::vector<T>& RequireVector(
        const std::shared_ptr<std::vector<T>>& value)
    {
        if (value == nullptr)
        {
            throw System::NullReferenceException();
        }
        return *value;
    }

    template <typename TInt>
    [[nodiscard]] std::string IntegerText(TInt value)
    {
        return std::to_string(value);
    }

#if defined(__linux__)
    [[nodiscard]] std::int64_t ProcStatusBytes(const char* label)
    {
        std::ifstream stream("/proc/self/status");
        if (!stream)
        {
            throw std::runtime_error("Process status could not be read.");
        }
        std::string key;
        while (stream >> key)
        {
            if (key == label)
            {
                std::uint64_t kibibytes = 0;
                std::string unit;
                if (!(stream >> kibibytes >> unit) || unit != "kB")
                {
                    throw std::runtime_error("Process status value was invalid.");
                }
                if (kibibytes
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max()) / 1024U)
                {
                    throw std::overflow_error("Process status value was too large.");
                }
                return static_cast<std::int64_t>(kibibytes * 1024U);
            }
            stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        throw std::runtime_error("Process status value was unavailable.");
    }
#endif
}

namespace MphRead::Mods::Network
{
    std::int32_t ServerSimCheck::Run(
        const std::string& room, std::int32_t players, double seconds, GameMode mode, bool formCheck)
    {
        players = std::clamp(players, 1, PlayerEntity::SlotCapacity);
        std::cout << "[simcheck] \"" << room << "\" (" << ::MphRead::ToString(mode) << "), "
            << IntegerText(players) << " player(s), " << ::MphRead::NativeRuntime::ToString(seconds, "0") << " s\n";

        std::int64_t snapshotBytes = 0;
        std::int64_t snapshots = 0;
        ServerSim sim{};
        std::string why;
        if (!ServerSim::Available(why))
        {
            std::cout << "[simcheck] cannot simulate: " << why << '\n';
            return 1;
        }

        const std::int64_t beforeLoad = WorkingSetBytes();
        const auto loadStart = std::chrono::steady_clock::now();
        std::int64_t matchEnds = 0;
        if (!sim.Start(room, mode, players,
            [&](std::span<const std::uint8_t> payload)
            {
                IncrementInPlace(snapshots);
                snapshotBytes = UncheckedAdd(
                    snapshotBytes, static_cast<std::int64_t>(payload.size()));
            },
            [&]()
            {
                IncrementInPlace(matchEnds);
            }, MakeNetSessionServerSimulationPort()))
        {
            return 1;
        }
        const auto loadStop = std::chrono::steady_clock::now();

        const std::int64_t afterLoad = WorkingSetBytes();
        ApplyRoster(players);
        const std::int32_t steps = ::MphRead::NativeRuntime::MathRoundToInt32(seconds * 60.0);
        IntentDriver driver(players);
        const auto wallStart = std::chrono::steady_clock::now();
        for (std::int32_t i = 0; i < steps; ++i)
        {
            driver.Feed(static_cast<std::uint32_t>(i + 1));
            sim.Step();
        }
        const auto wallStop = std::chrono::steady_clock::now();

        const std::int64_t afterRun = WorkingSetBytes();
        std::int32_t spawned = 0;
        const auto& playerList = PlayerEntity::Players();
        for (std::int32_t i = 0;
            i < players && i < static_cast<std::int32_t>(playerList.size()); ++i)
        {
            const std::shared_ptr<PlayerEntity>& player
                = playerList[static_cast<std::size_t>(i)];
            if (player == nullptr)
            {
                throw System::NullReferenceException();
            }
            if (TestFlag(player->LoadFlags(), LoadFlags::Spawned))
            {
                ++spawned;
            }
        }

        const double meanStep = sim.Frames() > 0
            ? sim.StepSeconds() / static_cast<double>(sim.Frames()) * 1000.0
            : 0.0;
        const double budget = meanStep / (1000.0 / 60.0) * 100.0;
        const double loadSeconds
            = std::chrono::duration<double>(loadStop - loadStart).count();
        const double wallSeconds
            = std::chrono::duration<double>(wallStop - wallStart).count();

        std::cout << '\n';
        std::cout << "SIMCHECK " << room
            << " | players " << IntegerText(players)
            << " | steps " << IntegerText(sim.Frames())
            << " | spawned " << IntegerText(spawned) << '/' << IntegerText(players)
            << " | snapshots " << IntegerText(snapshots) << " ("
            << ::MphRead::NativeRuntime::ToString(snapshots > 0
                ? static_cast<double>(snapshotBytes) / static_cast<double>(snapshots)
                : 0.0, "0")
            << " B mean)"
            << (matchEnds > 0 ? " | match ended" : "")
            << " | load " << ::MphRead::NativeRuntime::ToString(loadSeconds, "0.0") << " s"
            << " | step mean " << ::MphRead::NativeRuntime::ToString(meanStep, "0.00") << " ms ("
            << ::MphRead::NativeRuntime::ToString(budget, "0") << "% of budget)"
            << " worst " << ::MphRead::NativeRuntime::ToString(sim.WorstStepSeconds() * 1000.0, "0.0")
            << " ms overrun " << IntegerText(sim.OverrunSteps())
            << " | wall " << ::MphRead::NativeRuntime::ToString(wallSeconds, "0.0") << " s for "
            << ::MphRead::NativeRuntime::ToString(seconds, "0") << " s simulated"
            << " | rss " << Mb(beforeLoad) << "->" << Mb(afterLoad) << "->"
            << Mb(afterRun) << " MB"
            << " | peak " << Mb(PeakWorkingSetBytes()) << " MB\n";

        const bool formPassed = !formCheck || CheckStalledUnmorph();
        sim.Stop();
        return spawned == players && formPassed ? 0 : 1;
    }

    bool ServerSimCheck::CheckStalledUnmorph()
    {
        if (PlayerEntity::Players().empty())
        {
            std::cout << "FORMCHECK FAIL: no player\n";
            return false;
        }
        PlayerEntity& player = ::MphRead::NativeRuntime::RequireReference(PlayerEntity::Players()[0]);
        if (!::MphRead::TestFlag(player.LoadFlags(), Entities::LoadFlags::Spawned) || player.Health() == 0)
        {
            std::cout << "FORMCHECK FAIL: player did not spawn alive\n";
            return false;
        }

        player.ModForceForm(true);
        player.ExitAltForm();
        player.ModRefreshNodeRef(player.Position);
        const CollisionVolume expectedBiped = CollisionVolume::Move(
            PlayerEntity::PlayerVolumes[static_cast<std::size_t>(player.Hunter())][0], player.Position);
        const auto bipedIndex = [&player]()
        {
            return (*::MphRead::NativeRuntime::RequireReference(::MphRead::NativeRuntime::RequireReference(player.BipedModel2()).AnimInfo).Index)[0];
        };
        const bool prepared = !player.IsAltForm() && player.IsUnmorphing()
            && player.NodeRef != Formats::Culling::NodeRef::None
            && player.Volume().Equals(expectedBiped)
            && player.CameraType() == Entities::CameraType::First
            && bipedIndex() == static_cast<std::int32_t>(Entities::PlayerAnimation::Unmorph);
        const OpenTK::Mathematics::Vector3 position = player.Position;
        const CollisionVolume volume = player.Volume();
        ::MphRead::NativeRuntime::RequireReference(player.CameraInfo()).NodeRef = Formats::Culling::NodeRef::None;

        FormReconciliation reconciliation{};
        bool earlyForce = false;
        for (std::uint32_t frame = 0; frame <= 90; frame++)
        {
            const FormCorrection correction = reconciliation.Step(frame, false,
                player.IsAltForm(), player.IsMorphing(), player.IsUnmorphing(), 300);
            if (correction == FormCorrection::Force)
            {
                if (frame != 90)
                {
                    earlyForce = true;
                }
                player.ModForceForm(false);
            }
        }
        const OpenTK::Mathematics::Vector3 now = player.Position;
        const bool positionHeld = now.X == position.X && now.Y == position.Y && now.Z == position.Z;
        const bool nodeMatches = ::MphRead::NativeRuntime::RequireReference(player.CameraInfo()).NodeRef == player.NodeRef;
        const bool passed = prepared && !earlyForce && !player.IsAltForm()
            && !player.IsMorphing() && !player.IsUnmorphing()
            && positionHeld && player.Volume().Equals(volume)
            && player.Volume().Equals(expectedBiped)
            && player.CameraType() == Entities::CameraType::First
            && nodeMatches
            && bipedIndex() == static_cast<std::int32_t>(Entities::PlayerAnimation::Idle);
        const auto text = [](bool value) { return value ? "True" : "False"; };
        std::cout << "FORMCHECK " << (passed ? "PASS" : "FAIL") << ": "
            << "prepared=" << text(prepared) << " earlyForce=" << text(earlyForce)
            << " form=" << player.ModFormState() << " positionHeld=" << text(positionHeld)
            << " volumeHeld=" << text(player.Volume().Equals(volume))
            << " camera=" << ::MphRead::Entities::ToString(player.CameraType()) << " node=" << text(nodeMatches) << '\n';
        return passed;
    }

    void ServerSimCheck::ApplyRoster(std::int32_t players)
    {
        MatchStatePacket match{};
        match.MatchId = 1;
        match.AuthorityEpoch = 1;
        NetSession::ApplyMatchState(match, false);
        RosterPacket roster = RosterPacket::Create();
        roster.MatchId = 1;
        roster.AuthorityEpoch = 1;
        roster.Revision = 1;
        for (std::int32_t i = 0; i < players && i < RosterPacket::MaxSlots; ++i)
        {
            const std::size_t index = static_cast<std::size_t>(roster.Count);
            RequireVector(roster.Slots).at(index) = static_cast<std::uint8_t>(i);
            RequireVector(roster.Generations).at(index) = 1;
            RequireVector(roster.Hunters).at(index) = static_cast<std::uint8_t>(i % 7);
            RequireVector(roster.Colors).at(index) = 0;
            RequireVector(roster.Pings).at(index) = 0;
            RequireVector(roster.Names).at(index) = "SIM" + IntegerText(i + 1);
            roster.Count = static_cast<std::uint8_t>(roster.Count + 1U);
        }
        NetSession::ApplyRoster(roster);
    }

    ServerSimCheck::IntentDriver::IntentDriver(std::int32_t players)
        : _players(players),
          _at(static_cast<std::size_t>(players))
    {
    }

    void ServerSimCheck::IntentDriver::Feed(std::uint32_t frame)
    {
        const auto& players = PlayerEntity::Players();
        for (std::int32_t slot = 0; slot < _players; ++slot)
        {
            std::shared_ptr<PlayerEntity> player;
            if (slot < static_cast<std::int32_t>(players.size()))
            {
                player = players[static_cast<std::size_t>(slot)];
            }
            if (player != nullptr && TestFlag(player->LoadFlags(), LoadFlags::Spawned))
            {
                _at.at(static_cast<std::size_t>(slot)) = player->Position;
            }

            const double turn = static_cast<double>(frame) / 60.0
                + static_cast<double>(slot);
            const Vector3 aim(
                static_cast<float>(std::cos(turn)),
                0.0F,
                static_cast<float>(std::sin(turn)));
            IntentButtons buttons = IntentButtons::MoveUp;
            if (frame % 20U < 6U)
            {
                buttons |= IntentButtons::Shoot;
            }

            IntentPacket intent{};
            intent.Frame = frame;
            intent.MatchId = NetSession::CurrentMatchId();
            intent.AuthorityEpoch = NetSession::AuthorityEpoch();
            intent.SlotGeneration = NetPlayerLifecycle::Generation(slot);
            intent.LifeId = NetPlayerLifecycle::Get(slot);
            intent.Buttons = buttons;
            intent.Presses = std::make_shared<std::vector<std::uint32_t>>(
                static_cast<std::size_t>(IntentPacket::PressHistory));
            intent.Aim = aim;
            intent.Position = _at.at(static_cast<std::size_t>(slot));
            intent.WeaponSelect = 0xFFU;
            intent.AmmoUa = 400U;
            intent.AmmoMissiles = 50U;
            intent.AckFrame = frame > 6U ? frame - 6U : 0U;
            NetSession::AcceptSlotIntent(slot, std::move(intent));
        }
    }

    std::int64_t ServerSimCheck::WorkingSetBytes()
    {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (K32GetProcessMemoryInfo(
                GetCurrentProcess(), &counters, static_cast<DWORD>(sizeof(counters))) == 0)
        {
            throw std::runtime_error("Process working set could not be read.");
        }
        if (counters.WorkingSetSize
            > static_cast<SIZE_T>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::overflow_error("Process working set was too large.");
        }
        return static_cast<std::int64_t>(counters.WorkingSetSize);
#elif defined(__linux__)
        return ProcStatusBytes("VmRSS:");
#elif defined(__APPLE__)
        mach_task_basic_info_data_t info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        const kern_return_t result = task_info(
            mach_task_self(), MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&info), &count);
        if (result != KERN_SUCCESS)
        {
            throw std::runtime_error("Process working set could not be read.");
        }
        if (info.resident_size
            > static_cast<mach_vm_size_t>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::overflow_error("Process working set was too large.");
        }
        return static_cast<std::int64_t>(info.resident_size);
#else
#error Unsupported platform for Process.WorkingSet64 parity.
#endif
    }

    std::int64_t ServerSimCheck::PeakWorkingSetBytes()
    {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (K32GetProcessMemoryInfo(
                GetCurrentProcess(), &counters, static_cast<DWORD>(sizeof(counters))) == 0)
        {
            throw std::runtime_error("Process peak working set could not be read.");
        }
        if (counters.PeakWorkingSetSize
            > static_cast<SIZE_T>(std::numeric_limits<std::int64_t>::max()))
        {
            throw std::overflow_error("Process peak working set was too large.");
        }
        return static_cast<std::int64_t>(counters.PeakWorkingSetSize);
#elif defined(__linux__)
        return ProcStatusBytes("VmHWM:");
#elif defined(__APPLE__)
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0)
        {
            throw std::runtime_error("Process peak working set could not be read.");
        }
        return static_cast<std::int64_t>(usage.ru_maxrss);
#else
#error Unsupported platform for Process.PeakWorkingSet64 parity.
#endif
    }

    std::string ServerSimCheck::Mb(std::int64_t bytes)
    {
        return ::MphRead::NativeRuntime::ToString(static_cast<double>(bytes) / 1024.0 / 1024.0, "0");
    }
}
