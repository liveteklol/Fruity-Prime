#include "Mods/Network/Server/Match/ServerSim.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Diagnostics/NetShotDiagnostics.hpp"
#include "Mods/Network/Diagnostics/NetTimingDiagnostics.hpp"

#include "Formats/Formats.hpp"
#include "GameState.hpp"
#include "NativeRuntime/System/Runtime.hpp"
#include "Read.hpp"
#include "Scene.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Stopwatch.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/Headless.hpp"
#include "Mods/Input/SyntheticInput.hpp"
#include "Mods/Render/FrameTiming.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::UncheckedAdd;

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    void ServerSim::Advance(double now)
    {
        if (_scene == nullptr)
        {
            return;
        }
        if (_lastAdvance < 0.0)
        {
            _lastAdvance = now;
            return;
        }

        const double elapsed = now - _lastAdvance;
        _lastAdvance = now;
        if (elapsed > StallSeconds)
        {
            IncrementInPlace(_stalls);
            _accumulator = 0.0;
            return;
        }

        _accumulator += elapsed;
        std::int32_t steps = 0;
        while (_accumulator >= Mods::Render::FrameTiming::StepSeconds
            && steps < Mods::Render::FrameTiming::MaxCatchUpSteps)
        {
            _accumulator -= Mods::Render::FrameTiming::StepSeconds;
            Step();
            ++steps;
        }
        if (_accumulator >= Mods::Render::FrameTiming::StepSeconds)
        {
            const auto dropped = static_cast<std::int64_t>(
                _accumulator / Mods::Render::FrameTiming::StepSeconds);
            _droppedSteps = UncheckedAdd(_droppedSteps, dropped);
            _accumulator = 0.0;
        }
    }

    std::string ServerSim::Room() const
    {
        if (_scene == nullptr)
        {
            return "";
        }

        const std::int32_t roomId = (*_scene).RoomId();
        const RoomMetadata* metadata = Metadata::GetRoomById(roomId, true);
        const std::string current = metadata != nullptr ? metadata->Name : "";
        return !current.empty() ? current : _room;
    }

    bool ServerSim::Available(std::string& reason)
    {
        try
        {
            const std::string root = Paths::FileSystem();
            if (root.empty() || !MphRead::NativeRuntime::DirectoryExists(root))
            {
                reason = "no game files are set up on this machine (see paths.txt)";
                return false;
            }
        }
        catch (...)
        {
            reason = "game files could not be located ("
                + Runtime::ExceptionMessage(std::current_exception()) + ")";
            return false;
        }

        std::string inputReason;
        if (!Mods::Input::SyntheticInput::Available(inputReason))
        {
            reason = "a keyboard could not be synthesised (" + inputReason + ")";
            return false;
        }

        reason.clear();
        return true;
    }

    bool ServerSim::Start(const std::string& roomKey, GameMode mode,
        std::int32_t maxPlayers, SnapshotSink sink, std::function<void()> matchEnded,
        ServerSimulationSessionPort sessionPort,
        std::optional<RosterPacket> roster, std::optional<SessionStatePacket> session)
    {
        Stop();
        if (!sessionPort.StartAuthority || !sessionPort.Stop)
        {
            throw std::invalid_argument("server simulation requires a session lifecycle port");
        }
        _sessionPort = std::move(sessionPort);
        Mods::Headless::Enter();

        try
        {
            _room = roomKey;
            _mode = mode;

            const std::int32_t capacity = Entities::PlayerEntity::SlotCapacity;
            const std::int32_t clampedPlayers
                = maxPlayers < 2 ? 2 : (maxPlayers > capacity ? capacity : maxPlayers);
            Entities::PlayerEntity::SetMaxPlayers(clampedPlayers);

            _sessionStarted = true;
            _sessionPort.StartAuthority(std::move(sink), std::move(matchEnded), roster, session);

            auto keyboard = Mods::Input::SyntheticInput::CreateKeyboard();
            auto mouse = Mods::Input::SyntheticInput::CreateMouse();
            // The DS's own projection: nothing here builds one, so a stray
            // aspect ratio is at least the right one.
            std::shared_ptr<Scene> scene = std::make_shared<Scene>(
                OpenTK::Mathematics::Vector2i(256, 192), *keyboard, *mouse,
                [](std::string) {}, []() {});

            NetLaunch::BuildPlayers(*scene, Hunter::Samus, 0,
                GameState::IsTeamMode(mode), -1);
            scene->AddRoom(roomKey, mode, NetLaunch::RoomPlayerCount());
            scene->OnLoad();

            _scene = std::move(scene);
            NetHitClaims::SetScene(_scene.get());
            _frames = 0;
            _stepSeconds = 0.0;
            _worstStepSeconds = 0.0;
            _overrunSteps = 0;
            _stepFailures = 0;
            _droppedSteps = 0;
            _stalls = 0;
            _accumulator = 0.0;
            _lastAdvance = -1.0;
            return true;
        }
        catch (...)
        {
            const std::string error = Runtime::ExceptionToString(std::current_exception());
            std::cout << "[sim] could not load \"" << roomKey << "\": " << error << '\n';
            NetLog::Event("server simulation failed to start: " + error);
            Stop();
            Read::ClearCache();
            return false;
        }
    }

    void ServerSim::Step()
    {
        if (_scene == nullptr)
        {
            return;
        }

        const std::int64_t start = Runtime::StopwatchGetTimestamp();
        try
        {
            _scene->OnSimulationFrame();
        }
        catch (...)
        {
            const std::exception_ptr exception = std::current_exception();
            IncrementInPlace(_stepFailures);
            if (_stepFailures == 1)
            {
                std::cout << "[sim] step failed: " << Runtime::ExceptionToString(exception) << '\n';
            }
            else
            {
                std::cout << "[sim] step failed: " << Runtime::ExceptionMessage(exception) << '\n';
            }
            NetLog::Event("server simulation step failed: " + Runtime::ExceptionToString(exception));
        }

        const Runtime::TimeSpan elapsedTime{Runtime::StopwatchGetElapsedTicks(start)};
        const double elapsed = elapsedTime.TotalSeconds();
        IncrementInPlace(_frames);
        _stepSeconds += elapsed;
        if (elapsed > _worstStepSeconds)
        {
            _worstStepSeconds = elapsed;
        }
        if (elapsed > 1.0 / 60.0)
        {
            IncrementInPlace(_overrunSteps);
        }
    }

    void ServerSim::Stop()
    {
        if (_scene == nullptr && !_sessionStarted)
        {
            return;
        }
        if (_scene != nullptr)
        {
            NetHitClaims::SetScene(nullptr);
            _scene.reset();
        }
        _room.clear();
        if (_sessionStarted && _sessionPort.Stop)
        {
            _sessionPort.Stop();
        }
        _sessionStarted = false;
        _sessionPort = {};
        Read::ClearCache();
        NativeRuntime::ForceFullGc();
    }

    std::string ServerSim::DescribeUnlagged() const
    {
        return NetUnlagged::Describe() + "\n" + NetShotDiagnostics::Describe() + NetTimingDiagnostics::Describe();
    }

    std::string ServerSim::DescribeRewindDepths() const
    {
        return NetUnlagged::DescribeDepths();
    }

    std::optional<std::string> ServerSim::DescribeClaims() const
    {
        return NetHitClaims::Describe();
    }

    std::string ServerSim::DescribeAgreement() const
    {
        return NetHitClaims::DescribeAgreement();
    }

    std::string ServerSim::DescribeShots() const
    {
        std::string text = "shots spawned here (slot: beams):";
        bool any = false;
        for (std::size_t i = 0; i < NetDamageMetrics::Read().Fired.size(); i++)
        {
            if (NetDamageMetrics::Read().Fired[i] == 0)
            {
                continue;
            }
            any = true;
            text += " " + Runtime::ToString(static_cast<std::int32_t>(i))
                + ":" + Runtime::ToString(NetDamageMetrics::Read().Fired[i]);
        }
        return any ? text : std::string("shots spawned here: none");
    }

    std::string ServerSim::Describe() const
    {
        if (_scene == nullptr)
        {
            return "not simulating";
        }

        const double mean = _frames > 0
            ? _stepSeconds / static_cast<double>(_frames) * 1000.0
            : 0.0;

        std::string result = Room() + " (" + ::MphRead::ToString(GameState::Mode())
            + "), " + Runtime::ToString(_frames) + " step(s), "
            + Runtime::ToString(mean, "0.00") + " ms mean, "
            + Runtime::ToString(_worstStepSeconds * 1000.0, "0.0") + " ms worst, "
            + Runtime::ToString(_overrunSteps) + " overrun, "
            + Runtime::ToString(_droppedSteps) + " dropped, "
            + Runtime::ToString(_stalls) + " stall(s)";
        if (_stepFailures > 0)
        {
            result += ", " + Runtime::ToString(_stepFailures) + " FAILED";
        }
        return result;
    }
}
