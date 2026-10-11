#include "NetCheckClient.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/OpenTK/GL.hpp"
#include "HitLocation.hpp"
#include "HitRig.hpp"
#include "NetHitClaims.hpp"
#include "NetShotEvents.hpp"
#include "NetShotDiagnostics.hpp"
#include "NetSmoothing.hpp"
#include "NetTimingDiagnostics.hpp"
#include "../EndScreen.hpp"
#include "../MapPick.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"

#include "DemoClip.hpp"
#include "DemoRecorder.hpp"
#include "MapVote.hpp"
#include "NetFeatureCheck.hpp"
#include "NetDamage.hpp"
#include "NetHitPrediction.hpp"
#include "NetLag.hpp"
#include "NetLaunch.hpp"
#include "NetLog.hpp"
#include "NetProtocol.hpp"
#include "NetSession.hpp"
#include "NetTestScript.hpp"
#include "NetTransport.hpp"
#include "NetUnlagged.hpp"
#include "../SpectatorMode.hpp"
#include "../ScreenCapture.hpp"
#include "../Headless.hpp"
#include "../Chat/ChatBox.hpp"
#include "../../GameState.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Number.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../NativeRuntime/Rhi/BackendFactory.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

using ::MphRead::NativeRuntime::EnvironmentGetVariable;
using ::MphRead::NativeRuntime::ExceptionToString;
using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::NumberFormatInfo;
using ::MphRead::NativeRuntime::NumberStyles;
using ::MphRead::NativeRuntime::PathCombine;
using ::MphRead::NativeRuntime::TryParseDouble;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::MphRead::NativeRuntime::Utf16Length;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::Length;

namespace
{
    using MphRead::GameMode;
    using MphRead::Hunter;
    using MphRead::Entities::LoadFlags;
    using MphRead::Entities::PlayerFlags2;
    using MphRead::Mods::Network::TestPhase;
    using OpenTK::Mathematics::Vector3;

    [[nodiscard]] std::string TwoDigits(std::int32_t value)
    {
        std::ostringstream stream;
        stream << std::setw(2) << std::setfill('0') << value;
        return stream.str();
    }

    [[nodiscard]] const char* BoolText(bool value) noexcept
    {
        return value ? "True" : "False";
    }

    [[nodiscard]] std::string TestPhaseName(TestPhase phase)
    {
        return ::MphRead::Mods::Network::ToString(phase);
    }

    [[nodiscard]] std::string PadRightManaged(std::string value, std::size_t width)
    {
        const std::size_t length = Utf16Length(value);
        if (length < width)
        {
            value.append(width - length, ' ');
        }
        return value;
    }

    [[nodiscard]] std::string OptionalInterpolation(
        const std::optional<std::string>& value)
    {
        return value.has_value() ? *value : std::string();
    }
}

namespace MphRead::Mods::Network
{
    RendererPlatform::WindowSettings NetCheckClient::GameSettings()
    {
        RendererPlatform::WindowSettings settings{};
        settings.UpdateFrequency = 60.0;
        return settings;
    }

    RendererPlatform::WindowSettings NetCheckClient::WindowSettings(
        std::int32_t width, std::int32_t height)
    {
        RendererPlatform::WindowSettings settings = GameSettings();
        settings.ClientSize = OpenTK::Mathematics::Vector2i(width, height);
        settings.Title = "MphRead net check";
        settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
        settings.Flags = RendererPlatform::WindowSettings::ContextFlags::Default;
        settings.ApiMajor = 3;
        settings.ApiMinor = 2;
        settings.StartVisible = ShowWindow;
        settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::OpenGL;
        return settings;
    }

    void NetCheckClient::Run()
    {
        if (!_window)
        {
            RunHeadless();
            return;
        }
        _window->Run(*this);
    }

    void NetCheckClient::RunHeadless()
    {
        OnLoad();
        // The dedicated server's clock: one simulation step per 1/60 s of
        // wall time, catching up after a slow step rather than drifting, and
        // giving up on a backlog longer than a quarter second.
        using Clock = std::chrono::steady_clock;
        const auto step = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60.0));
        Clock::time_point next = Clock::now();
        while (!_headlessClosing)
        {
            HeadlessFrame();
            next += step;
            const Clock::time_point now = Clock::now();
            if (now - next > std::chrono::milliseconds(250))
            {
                next = now;
            }
            std::this_thread::sleep_until(next);
        }
        OnClosing();
    }

    void NetCheckClient::HeadlessFrame()
    {
        GameState::ApplyPause();
        _scene->OnSimulationFrame();
        IncrementInPlace(_frame);
        UpdateSpectating();
        DriveVoteTest();
        DriveRebindTest();
        Observe();
        _features->Observe(*_scene);
        SampleScoreboardOnServerClock();
        if (_frame >= _seconds * 60.0)
        {
            Close();
        }
    }

    void NetCheckClient::Dispose()
    {
        if (_scene)
        {
            _scene->ReleaseGpuResources();
            _scene.reset();
        }
        _swapchain.reset();
        _window.reset();
    }

    OpenTK::Mathematics::Vector2i NetCheckClient::ClientSize() const
    {
        return _window ? _window->Size() : OpenTK::Mathematics::Vector2i(256, 192);
    }

    void NetCheckClient::Close()
    {
        if (!_window)
        {
            _headlessClosing = true;
            return;
        }
        _window->Close();
    }

    void NetCheckClient::Present()
    {
        if (_swapchain)
        {
            _swapchain->Present();
        }
    }

    NetCheckClient::NetCheckClient(
        std::chrono::steady_clock::time_point wallClockStart,
        std::string name,
        std::string roomKey,
        MphRead::GameMode mode,
        MphRead::Hunter hunter,
        double seconds,
        std::optional<std::string> shotDirectory,
        std::int32_t width,
        std::int32_t height,
        double spectateAt,
        double rejoinAt,
        std::int32_t color)
        : _window(Headless ? nullptr : RendererPlatform::CreateWindow(WindowSettings(width, height))),
          _name(std::move(name)),
          _shotDirectory(std::move(shotDirectory)),
          _seconds(seconds),
          _spectateAt(spectateAt),
          _rejoinAt(rejoinAt),
          _wallClockStart(wallClockStart),
          _remoteSpectatingFrames(
              static_cast<std::size_t>(Entities::PlayerEntity::MaxPlayers()), 0),
          _remotes(static_cast<std::size_t>(Entities::PlayerEntity::MaxPlayers())),
          _features(std::make_unique<NetFeatureCheck>())
    {
        if (_window)
        {
            NativeRuntime::Rhi::SwapchainDesc swapchainDesc{};
            const OpenTK::Mathematics::Vector2i framebufferSize = _window->Size();
            swapchainDesc.width = static_cast<std::uint32_t>(std::max(framebufferSize.X, 1));
            swapchainDesc.height = static_cast<std::uint32_t>(std::max(framebufferSize.Y, 1));
            _swapchain = NativeRuntime::Rhi::BackendFactory::CreateSwapchain(
                NativeRuntime::Rhi::GraphicsBackend::OpenGl, *_window, swapchainDesc);
        }
        else
        {
            _headlessKeyboard = Mods::Input::SyntheticInput::CreateKeyboard();
            _headlessMouse = Mods::Input::SyntheticInput::CreateMouse();
        }
        for (std::unique_ptr<RemoteView>& remote : _remotes)
        {
            remote = std::make_unique<RemoteView>();
        }
        _scene = _window
            ? std::make_unique<MphRead::Scene>(
                _window->Size(),
                _window->Keyboard(),
                _window->Mouse(),
                [](auto&&) {},
                [this]() { Close(); })
            : std::make_unique<MphRead::Scene>(
                ClientSize(),
                *_headlessKeyboard,
                *_headlessMouse,
                [](auto&&) {},
                [this]() { Close(); });
        NetLaunch::BuildPlayers(*_scene, hunter, color, GameState::IsTeamMode(mode));
        _scene->AddRoom(roomKey, mode, NetLaunch::RoomPlayerCount());
    }

    NetCheckClient::~NetCheckClient() = default;

    MphRead::Scene& NetCheckClient::Scene() noexcept
    {
        return *_scene;
    }

    const MphRead::Scene& NetCheckClient::Scene() const noexcept
    {
        return *_scene;
    }

    void NetCheckClient::OnLoad()
    {
        _scene->Size(ClientSize());
        _scene->OnLoad();
        if (!_window)
        {
            return;
        }
        _window->BaseOnLoad();
        ::MphRead::NativeRuntime::Rhi::ResetWindowViewport(ClientSize().X, ClientSize().Y);
        _scene->OnResize();
    }

    void NetCheckClient::OnRenderFrame(const RendererPlatform::FrameEventArgs& args)
    {
        GameState::ApplyPause();
        _scene->OnUpdateFrame();
        if (!_scene->OnRenderFrame())
        {
            return;
        }
        IncrementInPlace(_frame);
        UpdateSpectating();
        DriveVoteTest();
        DriveRebindTest();
        Observe();
        _features->Observe(*_scene);
        SampleScoreboardOnServerClock();
        if (_shotDirectory.has_value() && _frame % 120 == 0)
        {
            const std::string path = PathCombine(
                *_shotDirectory, _name + "-" + TwoDigits(_shots) + ".png");
            if (Capture(path))
            {
                IncrementInPlace(_shots);
                _litFraction = std::max(
                    _litFraction, Mods::ScreenCapture::NonBlackFraction(_scene.get()));
            }
        }
        if (_shotDirectory.has_value() && ShowWindow && Mods::EndScreen::Available()
            && _frame % 60 == 0 && _endShots < 12)
        {
            if (Capture(PathCombine(*_shotDirectory, _name + "-end-" + TwoDigits(_endShots) + ".png")))
            {
                IncrementInPlace(_endShots);
            }
        }
        if (_shotDirectory.has_value() && _opponentInView && _duelShots < 8
            && UncheckedSubtract(_frame, _lastDuelShotFrame) > 45)
        {
            const std::string path = PathCombine(
                *_shotDirectory, _name + "-duel-" + TwoDigits(_duelShots) + ".png");
            if (Mods::ScreenCapture::Save(_scene.get(), path))
            {
                IncrementInPlace(_duelShots);
                _lastDuelShotFrame = _frame;
            }
        }
        Present();
        _scene->AfterRenderFrame();
        _window->BaseOnRenderFrame(args);
        if (_frame >= _seconds * 60.0)
        {
            Close();
        }
    }

    void NetCheckClient::SampleScoreboardOnServerClock()
    {
        const std::optional<MatchStatePacket> serverMatch = NetSession::ServerMatch();
        if (!serverMatch.has_value())
        {
            return;
        }
        const float elapsed = serverMatch->TimeElapsed;
        if (_scoreboardSampleAt < 0.0F)
        {
            _scoreboardSampleAt = static_cast<float>(
                std::ceil((elapsed + 20.0F) / SampleEvery) * SampleEvery);
            return;
        }
        if (elapsed >= _scoreboardSampleAt)
        {
            _features->SampleScoreboard(static_cast<std::int32_t>(_scoreboardSampleAt));
            _scoreboardSampleAt += SampleEvery;
        }
    }

    void NetCheckClient::UpdateSpectating()
    {
        if (_spectateAt >= 0.0 && _spectateStartedFrame < 0
            && _frame >= _spectateAt * 60.0)
        {
            SpectatorMode::Start();
            if (SpectatorMode::IsSpectating())
            {
                _spectateStartedFrame = _frame;
                std::cout
                    << "[netcheck] " << _name
                    << " is spectating from frame " << _frame << '\n';
            }
            else
            {
                _spectateStartedFrame = std::numeric_limits<std::int32_t>::max();
                std::cout
                    << "[netcheck] " << _name
                    << " could not spectate (multiplayer="
                    << BoolText(GameState::Multiplayer()) << ")\n";
            }
        }
        if (_rejoinAt >= 0.0 && _rejoinedFrame < 0 && SpectatorMode::IsSpectating()
            && _frame >= _rejoinAt * 60.0)
        {
            SpectatorMode::Rejoin();
            _rejoinedFrame = _frame;
            std::cout
                << "[netcheck] " << _name
                << " rejoined the match on frame " << _frame << '\n';
        }
        if (SpectatorMode::IsSpectating())
        {
            IncrementInPlace(_spectatingFrames);
        }
        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            if (slot == std::max(NetSession::LocalSlot(), 0)
                || static_cast<std::size_t>(slot) >= Entities::PlayerEntity::Players().size())
            {
                continue;
            }
            const std::shared_ptr<Entities::PlayerEntity>& player
                = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(slot));
            if (TestFlag(player->Flags2(), PlayerFlags2::Spectating))
            {
                IncrementInPlace(_remoteSpectatingFrames.at(static_cast<std::size_t>(slot)));
            }
        }
    }

    void NetCheckClient::DriveVoteTest()
    {
        const std::optional<std::string> room = EnvironmentGetVariable("MPHREAD_VOTE_TEST");
        if (!room.has_value())
        {
            return;
        }
        if (MapVote::Active() && !MapVote::Answered())
        {
            std::cout
                << "[votetest] " << _name
                << " sees " << MapVote::Proposer().value_or("")
                << " propose " << MapVote::RoomKey().value_or("")
                << " (" << MapVote::Yes() << '/' << MapVote::Needed()
                << " of " << MapVote::Eligible() << ")\n";
            MapVote::Cast(true);
            return;
        }
        if (!_votedOnce && !room->empty() && _frame == 600)
        {
            _votedOnce = true;
            std::cout << "[votetest] " << _name << " proposes " << *room << '\n';
            MapVote::Propose(*room);
        }
    }

    void NetCheckClient::DriveRebindTest()
    {
        if (_rebound)
        {
            return;
        }
        const std::optional<std::string> at = EnvironmentGetVariable("MPHREAD_NET_REBIND");
        double seconds = 0.0;
        if (!at.has_value() || !TryParseDouble(*at,
            NumberStyles::Float | NumberStyles::AllowThousands,
            NumberFormatInfo::InvariantInfo(), seconds))
        {
            return;
        }
        if (_frame < seconds * 60.0)
        {
            return;
        }
        _rebound = true;
        std::cout
            << "[rebindtest] " << _name
            << " was slot " << NetSession::LocalSlot() << '\n';
        NetSession::RebindSocket();
    }

    bool NetCheckClient::Capture(const std::string& path)
    {
        return ShowWindow
            ? Mods::ScreenCapture::SaveWindow(_scene.get(), path)
            : Mods::ScreenCapture::Save(_scene.get(), path);
    }

    void NetCheckClient::VoteOnMap()
    {
        if (MapVoteRow < 0 || !Mods::MapPick::Available())
        {
            return;
        }
        const std::vector<std::string>& order = Mods::MapPick::Order();
        std::string want = order[0];
        if (Mods::MapPick::VotesFor(want) == 0)
        {
            want = order[static_cast<std::size_t>(std::min(MapVoteRow, static_cast<std::int32_t>(order.size()) - 1))];
        }
        if (!::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(Mods::MapPick::Picked(), want))
        {
            if (want != _lastBallotRoom)
            {
                _lastBallotRoom = want;
                IncrementInPlace(_mapVotesCast);
                std::cout << "[mapvote] " << _name << " picked " << want << '\n';
            }
            Mods::MapPick::Choose(Mods::MapPick::IndexOf(want));
        }
        if (::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase(Mods::EndScreen::NextRoomKey(), want)
            && _mapVotesCarried < _mapVotesCast)
        {
            IncrementInPlace(_mapVotesCarried);
            std::cout << "[mapvote] " << _name << " sees the server agree: next is " << want << '\n';
        }
    }

    void NetCheckClient::Observe()
    {
        _opponentInView = false;
        VoteOnMap();
        if (_scene->RoomId() != _lastRoomId)
        {
            if (_lastRoomId != -1)
            {
                IncrementInPlace(_roomChanges);
                _everSawSomeone |= AnyoneSeen();
                _features->Reset();
                for (std::unique_ptr<RemoteView>& remote : _remotes)
                {
                    remote = std::make_unique<RemoteView>();
                }
                _localSpawnFrame = -1;
                _lastLocalHealth = -1;
                _wasAliveLocal = false;
            }
            _lastRoomId = _scene->RoomId();
        }
        SayHello();
        const std::int32_t local = std::max(NetSession::LocalSlot(), 0);
        std::shared_ptr<Entities::PlayerEntity> me{};
        if (static_cast<std::size_t>(local) < Entities::PlayerEntity::Players().size())
        {
            me = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(local));
        }
        if (me && TestFlag(me->LoadFlags(), LoadFlags::Spawned))
        {
            if (_localSpawnFrame < 0)
            {
                _localSpawnFrame = _frame;
            }
            _minHealthSeen = std::min(_minHealthSeen, me->Health());
            if (_wasAliveLocal && me->Health() == 0)
            {
                IncrementInPlace(_myDeaths);
            }
            if (_lastLocalHealth > 0 && me->Health() > 0 && me->Health() < _lastLocalHealth)
            {
                IncrementInPlace(_damageTaken);
            }
            _lastLocalHealth = me->Health();
            _wasAliveLocal = me->Health() > 0;
            if (me->IsAltForm())
            {
                IncrementInPlace(_myAltFrames);
            }
            if (me->ModDamageIndicatorActive())
            {
                IncrementInPlace(_indicatorFrames);
            }
        }
        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            if (slot == local
                || static_cast<std::size_t>(slot) >= Entities::PlayerEntity::Players().size())
            {
                continue;
            }
            const std::shared_ptr<Entities::PlayerEntity>& other
                = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(slot));
            RemoteView& view = *_remotes.at(static_cast<std::size_t>(slot));
            if (!TestFlag(other->LoadFlags(), LoadFlags::Active))
            {
                continue;
            }
            IncrementInPlace(view.FramesActive);
            if (!TestFlag(other->LoadFlags(), LoadFlags::Spawned))
            {
                continue;
            }
            IncrementInPlace(view.FramesSpawned);
            if (view.FirstSpawnFrame < 0)
            {
                view.FirstSpawnFrame = _frame;
            }
            view.MinHealth = std::min(view.MinHealth, other->Health());
            if (view.WasAlive && other->Health() == 0)
            {
                IncrementInPlace(view.Deaths);
            }
            if (view.LastHealth > 0 && other->Health() > 0 && other->Health() < view.LastHealth)
            {
                IncrementInPlace(view.Hits);
            }
            view.LastHealth = other->Health();
            view.WasAlive = other->Health() > 0;
            if (view.HavePosition)
            {
                const float step = Length(other->Position - view.LastPosition);
                if (step < 5.0F)
                {
                    view.Travelled += step;
                }
                if (step > 0.05F)
                {
                    IncrementInPlace(view.DistinctPositions);
                }
            }
            view.LastPosition = other->Position;
            view.HavePosition = true;
            view.Hunter = other->Hunter();
            if (other->IsAltForm())
            {
                IncrementInPlace(view.AltFormFrames);
            }
            if (NetSession::RemoteStateValid.at(static_cast<std::size_t>(slot)))
            {
                const bool wanted
                    = (NetSession::RemoteStates.at(static_cast<std::size_t>(slot)).Flags
                        & PlayerState::FlagAltForm) != 0;
                if (wanted)
                {
                    IncrementInPlace(view.AltFormWantedFrames);
                }
                if (wanted != other->IsAltForm())
                {
                    IncrementInPlace(view.AltFormDisagreeFrames);
                }
            }
            if (me && other->Health() > 0 && !_opponentInView)
            {
                const auto [turnX, turnY] = me->ModAimDeltaTowards(other->ModAimTarget());
                const float distance = Length(static_cast<OpenTK::Mathematics::Vector3>(other->Position) - static_cast<OpenTK::Mathematics::Vector3>(me->Position));
                _opponentInView = distance < 25.0F && std::fabs(turnX) < 18.0F
                    && std::fabs(turnY) < 18.0F;
            }
        }
    }

    void NetCheckClient::OnClosing()
    {
        _scene->DoCleanup();
        if (_window)
        {
            _window->BaseOnClosing();
        }
    }

    void NetCheckClient::SayHello()
    {
        if (NetSession::LocalSlot() < 0 || (_frame != 300 && _frame != 1500))
        {
            return;
        }
        Mods::Chat::ChatBox::Send(
            "hello from " + _name + " at frame " + std::to_string(_frame));
    }

    bool NetCheckClient::Passed() const
    {
        return _everSawSomeone || AnyoneSeen();
    }

    bool NetCheckClient::AnyoneSeen() const
    {
        for (const std::unique_ptr<RemoteView>& remote : _remotes)
        {
            const RemoteView& view = *remote;
            if (view.FirstSpawnFrame >= 0 && view.DistinctPositions > 5
                && view.Travelled > 2.0)
            {
                return true;
            }
        }
        return false;
    }

    double NetCheckClient::ElapsedSeconds() const
    {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - _wallClockStart).count();
    }

    double NetCheckClient::FramesPerSecond() const
    {
        return ElapsedSeconds() > 0.0 ? _frame / ElapsedSeconds() : 0.0;
    }

    void NetCheckClient::Report()
    {
        std::cout
            << "  ran " << _frame << " frame(s) in "
            << ::MphRead::NativeRuntime::ToString(ElapsedSeconds(), "0.0") << " s -- "
            << ::MphRead::NativeRuntime::ToString(FramesPerSecond(), "0.0") << " fps\n";
        const std::int32_t local = std::max(NetSession::LocalSlot(), 0);
        std::cout << '\n';
        std::cout << "=== " << _name << ": what this client saw ===\n";
        const std::optional<std::string> lag = NetLag::Describe();
        if (lag.has_value())
        {
            std::cout
                << "  SIMULATED LINE: " << *lag << " -- these numbers "
                << "describe a reproduction, not a real connection\n";
        }
        std::cout
            << "  slot " << local
            << ", authority=" << BoolText(NetSession::IsAuthority())
            << ", frames=" << _frame << '\n';
        std::cout << NetShotDiagnostics::Describe() << '\n';
        std::cout << NetTimingDiagnostics::Describe() << '\n';
        std::cout << "  " << NetUnlagged::Describe() << '\n';
        std::cout << "  " << NetUnlagged::DescribeDepths() << '\n';
        std::cout << "  " << NetHitPrediction::Describe() << '\n';
        std::cout << "  " << NetHitPrediction::DescribeHeadshots() << '\n';
        std::cout << "  " << NetHitPrediction::DescribeHealth() << '\n';
        std::cout << "  " << NetHitPrediction::DescribeDamageLedger() << '\n';
        for (const std::string& line : ::MphRead::NativeRuntime::StringSplit(NetHitPrediction::DescribeByWeapon(), '\n'))
        {
            std::cout << "  " << line << '\n';
        }
        if (const std::optional<std::string> claims = NetHitClaims::Describe(); claims.has_value())
        {
            std::cout << "  " << *claims << '\n';
        }
        if (const std::optional<std::string> events = NetShotEvents::Describe(); events.has_value())
        {
            std::cout << "  " << *events << '\n';
        }
        // Per shooter, for tools/netcheck/compare-reports.py: every event
        // received here has to have been fired.
        const std::int32_t me = std::max(NetSession::LocalSlot(), 0);
        for (std::int32_t slot = 0; slot < NetShotEvents::Slots; ++slot)
        {
            const ShotQueueStats shots = NetShotEvents::Stats(slot);
            if (slot == me || (shots.Received == 0 && shots.Gaps == 0))
            {
                continue;
            }
            std::cout << "  netcheck-shots " << GameState::Nicknames().at(static_cast<std::size_t>(me))
                << " from " << GameState::Nicknames().at(static_cast<std::size_t>(slot))
                << " received " << shots.Received << " fired " << shots.Fired
                << " stale " << shots.Stale << " pushed " << shots.Overflow
                << " abandoned " << shots.Abandoned << " waiting " << shots.Waiting
                << " overdue " << shots.Overdue
                << " gaps " << shots.Gaps << " recovered " << shots.Recovered
                << " lost " << shots.Lost << " pending " << shots.Pending
                << " late " << shots.OutOfOrder << '\n';
        }
        if (const std::optional<std::string> smoothing = NetSmoothing::Describe(); smoothing.has_value())
        {
            std::cout << "  " << *smoothing << '\n';
        }
        if (HitRig::Active())
        {
            std::cout << "  " << HitRig::Describe() << '\n';
            if (HitRig::Mode() == HitRig::RigMode::Strafe)
            {
                std::cout << "  " << HitLocation::DescribeWatch() << '\n';
            }
        }

        const RoomMetadata* roomMetadata
            = Metadata::GetRoomById(_scene->RoomId(), true);
        const std::optional<MatchStatePacket> serverMatch = NetSession::ServerMatch();
        const std::string serverRoom = serverMatch.has_value() && serverMatch->RoomKey.has_value()
            ? *serverMatch->RoomKey
            : std::string("?");
        std::cout
            << "  room: " << (roomMetadata ? roomMetadata->Name : std::string("?"))
            << " (server says " << serverRoom << "), "
            << _roomChanges << " rotation(s) followed\n";
        if (MapVoteRow >= 0)
        {
            std::cout << "  map votes: " << _mapVotesCast << " cast, "
                << _mapVotesCarried << " carried by the server\n";
        }
        std::cout
            << "  packets: snapshots sent=" << NetSession::SnapshotsSent()
            << " received=" << NetSession::SnapshotsReceived()
            << " late=" << NetSession::SnapshotsOutOfOrder()
            << " restreams=" << NetSession::SnapshotStreamResets()
            << " intents received=" << NetSession::IntentsReceived()
            << " intents late=" << NetSession::IntentsOutOfOrder()
            << " states applied=" << NetSession::StatesApplied()
            << " dropped=" << NetTransport::TotalPacketsDropped.load()
            << '\n';
        std::cout
            << "  chat: sent=" << Mods::Chat::ChatBox::Sent()
            << " received=" << Mods::Chat::ChatBox::Received() << '\n';

        std::ostringstream pings;
        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            if (NetSession::SlotOccupied.at(static_cast<std::size_t>(slot)))
            {
                if (pings.tellp() > 0)
                {
                    pings << "  ";
                }
                pings
                    << "slot " << slot << ' '
                    << NetSession::SlotPing.at(static_cast<std::size_t>(slot)) << " ms";
            }
        }
        if (NetSession::ReAnnouncements() > 0 || NetSession::LongestServerSilence() > 1.0)
        {
            std::cout
                << "  server silence: " << NetSession::ReAnnouncements()
                << " re-announce(s), longest gap "
                << ::MphRead::NativeRuntime::ToString(NetSession::LongestServerSilence(), "0.0")
                << " s of engine time ("
                << ::MphRead::NativeRuntime::ToString(NetSession::LongestServerSilence() * 60.0
                        / std::max(FramesPerSecond(), 1.0), "0.0")
                << " s of wall clock at this client's "
                << ::MphRead::NativeRuntime::ToString(FramesPerSecond(), "0") << " fps), "
                << NetSession::AuthorityStandDowns() << " authority stand-down(s)\n";
        }
        if (pings.tellp() > 0)
        {
            std::cout << "  pings: " << pings.str() << '\n';
        }

        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            if (static_cast<std::size_t>(slot) >= Entities::PlayerEntity::Players().size())
            {
                continue;
            }
            const std::shared_ptr<Entities::PlayerEntity>& player
                = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(slot));
            const bool active = TestFlag(player->LoadFlags(), LoadFlags::Active);
            if (!active && !NetSession::SlotOccupied.at(static_cast<std::size_t>(slot)))
            {
                continue;
            }
            const std::string nickname = GameState::Nicknames().at(static_cast<std::size_t>(slot));
            std::cout
                << "  slot " << slot << ' '
                << PadRightManaged(nickname, 10) << ' '
                << PadRightManaged(::MphRead::ToString(player->Hunter()), 8) << ' '
                << "active=" << (active ? "y" : "n") << ' '
                << "spawned="
                << (TestFlag(player->LoadFlags(), LoadFlags::Spawned) ? "y" : "n") << ' '
                << "hp=" << PadRightManaged(std::to_string(player->Health()), 4) << ' '
                << "pos=(" << ::MphRead::NativeRuntime::ToString(player->Position.X, "0.0")
                << ',' << ::MphRead::NativeRuntime::ToString(player->Position.Y, "0.0")
                << ',' << ::MphRead::NativeRuntime::ToString(player->Position.Z, "0.0") << ")\n";
        }

        std::cout << "  my player: ";
        if (static_cast<std::size_t>(local) < Entities::PlayerEntity::Players().size())
        {
            std::cout << ::MphRead::ToString(
                Entities::PlayerEntity::Players().at(static_cast<std::size_t>(local))->Hunter());
        }
        else
        {
            std::cout << '?';
        }
        std::cout << ", alt form on " << _myAltFrames << " frame(s)\n";
        std::cout
            << "  my player: spawned on frame " << _localSpawnFrame
            << ", lowest health "
            << (_minHealthSeen == std::numeric_limits<std::int32_t>::max()
                ? -1 : _minHealthSeen)
            << ", died " << _myDeaths << " time(s), took a hit "
            << _damageTaken << " time(s)\n";
        std::cout
            << "  HUD damage indicator lit on " << _indicatorFrames << " frame(s)\n";

        for (std::size_t slot = 0; slot < _remotes.size(); ++slot)
        {
            const RemoteView& view = *_remotes[slot];
            if (view.FramesActive == 0)
            {
                continue;
            }
            std::cout
                << "  slot " << slot << " (" << GameState::Nicknames().at(slot)
                << ") as I saw them: " << ::MphRead::ToString(view.Hunter)
                << ", active " << view.FramesActive << " frame(s), spawned "
                << view.FramesSpawned << ", first on frame " << view.FirstSpawnFrame
                << ", in alt form for " << view.AltFormFrames
                << " (authority said alt form on " << view.AltFormWantedFrames
                << ", disagreed on " << view.AltFormDisagreeFrames << ")\n";
            std::cout
                << "    moved " << ::MphRead::NativeRuntime::ToString(view.Travelled, "0.0") << " units over "
                << view.DistinctPositions << " distinct position(s); I saw them hit "
                << view.Hits << " time(s), killed " << view.Deaths
                << " time(s), lowest health "
                << (view.MinHealth == std::numeric_limits<std::int32_t>::max()
                    ? -1 : view.MinHealth)
                << '\n';
        }

        if (_spectateAt >= 0.0 || _spectatingFrames > 0)
        {
            std::cout
                << "  spectating: " << _spectatingFrames << " frame(s), started on frame "
                << (_spectateStartedFrame == std::numeric_limits<std::int32_t>::max()
                    ? -1 : _spectateStartedFrame)
                << ", rejoined on frame " << _rejoinedFrame
                << ", now=" << BoolText(SpectatorMode::IsSpectating()) << '\n';
        }
        for (std::size_t slot = 0; slot < _remoteSpectatingFrames.size(); ++slot)
        {
            if (_remoteSpectatingFrames[slot] > 0)
            {
                std::cout
                    << "  slot " << slot << " (" << GameState::Nicknames().at(slot)
                    << ") was spectating on " << _remoteSpectatingFrames[slot]
                    << " of my frame(s)\n";
            }
        }
        std::cout
            << "  script: " << NetTestScript::FramesOnTarget()
            << " frame(s) with somebody in its sights, phase now "
            << TestPhaseName(NetTestScript::Phase()) << '\n';
        if (_shots > 0)
        {
            std::cout
                << "  " << _shots << " screenshot(s) written to "
                << OptionalInterpolation(_shotDirectory)
                << ", of which " << _duelShots
                << " with an opponent in view, busiest frame "
                << ::MphRead::NativeRuntime::ToString(_litFraction * 100.0, "0.0") << "% lit\n";
        }
        if (HitRig::Mode() == HitRig::RigMode::Dialanche)
        {
            // This rig tests one-way alt damage. The tour's bilateral gun,
            // jump, and facing requirements do not describe this scenario.
            bool ok = false;
            for (const auto& player : Entities::PlayerEntity::Players())
            {
                if (!player || player->Hunter() == Hunter::Spire
                    || !TestFlag(player->LoadFlags(), Entities::LoadFlags::Active)) continue;
                const auto slot = static_cast<std::size_t>(player->SlotIndex());
                const auto& state = NetSession::RemoteStates[slot];
                const int replayed = NetDamage::Replayed[slot];
                ok = NetSession::IsClient() && NetSession::RemoteStateValid[slot]
                    && NetSession::SnapshotsReceived() > 3 && replayed >= 2
                    && replayed == state.DamageEventId && player->Health() == state.Health;
                std::cout << "  DIALANCHE LIVE " << (ok ? "PASS" : "FAIL")
                    << " victim=" << slot << " HP=" << player->Health()
                    << " authorityHP=" << state.Health << " events=" << replayed
                    << " latestEvent=" << state.DamageEventId << '\n';
                break;
            }
            _featureFailures = ok ? 0 : 1;
            std::cout << "  RESULT: " << (Passed() && ok ? "PASS" : "FAIL") << '\n';
            return;
        }
        std::int32_t featureFailures = 0;
        const bool featuresOk = _features->Report(featureFailures);
        _featureFailures = featureFailures;
        std::cout << '\n';
        if (Passed() && featuresOk)
        {
            std::cout << "  RESULT: PASS\n";
        }
        else
        {
            std::cout << "  RESULT: FAIL -- ";
            if (!Passed())
            {
                std::cout << "no other player was on the map and moving; ";
            }
            std::cout << featureFailures << " check(s) failed\n";
        }
    }

    std::int32_t NetCheckClient::Run(
        std::string host,
        std::int32_t port,
        std::string name,
        MphRead::Hunter hunter,
        double seconds,
        std::optional<std::string> shotDirectory,
        std::int32_t width,
        std::int32_t height,
        bool recordDemo,
        double spectateAt,
        double rejoinAt,
        std::int32_t color)
    {
        if (Headless)
        {
            Mods::Headless::Enter();
            std::cout << "[netcheck] " << name << " runs headless: no window, no GPU\n";
        }
        if (!NetLaunch::Join(host, port, name, hunter, 8000, color))
        {
            std::cout << "[netcheck] " << name << " could not join\n";
            NetSession::Stop();
            return 1;
        }
        NetTestScript::Reset();
        NetTestScript::SetEnabled(true);

        const auto roomOptional = NetLaunch::ServerRoom();
        const auto [roomKey, roomMode] = roomOptional.value();
        std::cout
            << "[netcheck] " << name
            << " joined slot " << NetSession::LocalSlot()
            << ", loading " << roomKey << " (" << ::MphRead::ToString(roomMode) << ")\n";
        if (recordDemo && DemoRecorder::Start())
        {
            std::cout
                << "[netcheck] " << name << " is recording to "
                << OptionalInterpolation(DemoRecorder::CurrentPath()) << '\n';
        }

        std::unique_ptr<NetCheckClient> window{};
        std::int32_t result = 0;
        try
        {
            window = std::unique_ptr<NetCheckClient>(new NetCheckClient(
                std::chrono::steady_clock::now(),
                name,
                roomKey,
                roomMode,
                hunter,
                seconds,
                shotDirectory,
                width,
                height,
                spectateAt,
                rejoinAt,
                NetSession::LocalColor()));
            window->Run();
            window->Report();
            result = window->Passed() && window->_featureFailures == 0 ? 0 : 1;
        }
        catch (...)
        {
            std::cout
                << "[netcheck] " << name << " crashed: "
                << ExceptionToString(std::current_exception()) << '\n';
            result = 2;
        }

        if (DemoRecorder::IsRecording())
        {
            std::cout
                << "[netcheck] " << name << " recorded "
                << OptionalInterpolation(DemoRecorder::CurrentPath()) << '\n';
            DemoRecorder::Stop();
        }
        if (EnvironmentGetVariable("MPHREAD_CLIP_TEST").has_value())
        {
            const double held = DemoClip::Held();
            const std::optional<std::string> first = DemoClip::Save();
            std::cout
                << "[netcheck] " << name << " clip held "
                << ::MphRead::NativeRuntime::ToString(held, "0.0") << " s, "
                << (first.has_value() ? *first : std::string("nothing saved")) << '\n';
            const std::optional<std::string> second = DemoClip::Save();
            std::cout
                << "[netcheck] " << name << " clip again -> "
                << (second.has_value() ? *second : std::string("nothing saved")) << '\n';
        }
        if (window)
        {
            window->Dispose();
        }
        SpectatorMode::Reset();
        NetTestScript::SetEnabled(false);
        NetSession::Stop();
        NetLog::Close();
        return result;
    }
}
