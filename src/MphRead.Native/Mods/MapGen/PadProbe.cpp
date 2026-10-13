#include "PadProbe.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/OpenTK/GL.hpp"
#include "../../GameState.hpp"
#include "../../Scene.hpp"
#include "../../Entities/JumpPadEntity.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Formats/Types.hpp"
#include "../Network/Testing/Maps/MapAudit.hpp"
#include "../Network/Integration/Launch/NetLaunch.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../NativeRuntime/Rhi/BackendFactory.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MphRead::Mods::MapGen
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::JumpPadEntity;
    using Entities::Keybind;
    using Entities::LoadFlags;
    using Entities::PlayerEntity;
    using Entities::PlayerFlags1;
    using OpenTK::Mathematics::Vector2i;
    using OpenTK::Mathematics::Vector3;

    namespace
    {
        [[nodiscard]] std::string F(float value, std::string_view format)
        {
            return Runtime::ToString(value, format);
        }

        [[nodiscard]] std::string Right(const std::string& text, std::size_t width)
        {
            return Runtime::StringPadLeft(text, width);
        }

        [[nodiscard]] float Distance(Vector3 a, Vector3 b) noexcept
        {
            const float x = a.X - b.X;
            const float y = a.Y - b.Y;
            const float z = a.Z - b.Z;
            return std::sqrt(x * x + y * y + z * z);
        }

        [[nodiscard]] float Across(Vector3 a, Vector3 b) noexcept
        {
            const float x = a.X - b.X;
            const float z = a.Z - b.Z;
            return std::sqrt(x * x + z * z);
        }
    }

    RendererPlatform::WindowSettings PadProbe::WindowSettings()
    {
        RendererPlatform::WindowSettings settings{};
        settings.UpdateFrequency = 60;
        settings.ClientSize = Vector2i(320, 180);
        settings.Title = "MphRead jump pad probe";
        settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
        settings.Flags = RendererPlatform::WindowSettings::ContextFlags::Default;
        settings.ApiMajor = 3;
        settings.ApiMinor = 2;
        settings.StartVisible = false;
        settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::OpenGL;
        return settings;
    }

    PadProbe::PadProbe(std::string room, Hunter hunter, bool alt)
        : _window(RendererPlatform::CreateWindow(WindowSettings())),
          _room(std::move(room)),
          _hunter(hunter),
          _alt(alt)
    {
        NativeRuntime::Rhi::SwapchainDesc swapchainDesc{};
        const Vector2i framebufferSize = _window->Size();
        swapchainDesc.width = static_cast<std::uint32_t>(std::max(framebufferSize.X, 1));
        swapchainDesc.height = static_cast<std::uint32_t>(std::max(framebufferSize.Y, 1));
        _swapchain = NativeRuntime::Rhi::BackendFactory::CreateSwapchain(
            NativeRuntime::Rhi::GraphicsBackend::OpenGl, *_window, swapchainDesc);
        Network::MapAudit::ForceEveryone(true);
        PlayerEntity::SetMaxPlayers(std::max<std::int32_t>(PlayerEntity::MaxPlayers(), Slot + 1));
        _scene = std::make_unique<MphRead::Scene>(ClientSize(), _window->Keyboard(), _window->Mouse(),
            [](std::string) {}, [this]() { Close(); });
        // Slot 0 exists only to be the main player and stand still.
        _scene->AddPlayer(Hunter::Samus, 0, -1);
        _scene->AddPlayer(hunter, 0, -1);
        const auto& players = PlayerEntity::Players();
        for (std::size_t i = 0; i < players.size(); i++)
        {
            PlayerEntity& player = Runtime::RequireReference(players[i]);
            player.SetIsBot(false);
            player.SetBotLevel(0);
            if (static_cast<std::int32_t>(i) > Slot)
            {
                player.SetLoadFlags(player.LoadFlags() & ~LoadFlags::Active);
            }
        }
        PlayerEntity::SetPlayerCount(Slot + 1);
        PlayerEntity::SetMainPlayerIndex(0);
        _scene->AddRoom(_room, GameMode::Battle, Network::NetLaunch::RoomPlayerCount());
    }

    PadProbe::~PadProbe() = default;

    Vector2i PadProbe::ClientSize() const
    {
        return _window->Size();
    }

    void PadProbe::Close()
    {
        _window->Close();
    }

    void PadProbe::OnLoad()
    {
        _scene->Size(ClientSize());
        _scene->OnLoad();
        _window->BaseOnLoad();
        ::MphRead::NativeRuntime::Rhi::ResetWindowViewport(ClientSize().X, ClientSize().Y);
        _scene->OnResize();
    }

    void PadProbe::OnRenderFrame(const RendererPlatform::FrameEventArgs& args)
    {
        GameState::ApplyPause();
        _scene->OnSimulationFrame();
        _scene->OnDrawFrame();
        if (!_scene->OnRenderFrame())
        {
            return;
        }
        Step();
        _swapchain->Present();
        _scene->AfterRenderFrame();
        _window->BaseOnRenderFrame(args);
    }

    void PadProbe::Controls(PlayerEntity& player, bool morph, bool forward)
    {
        Entities::PlayerControls& c = player.Controls();
        const auto& all = c.All();
        if (_wasDown.size() < all.size())
        {
            _wasDown.assign(all.size(), false);
        }
        for (std::size_t i = 0; i < all.size(); i++)
        {
            Keybind& bind = Runtime::RequireReference(Runtime::ManagedAt(all, i));
            bind.SetIsDown((morph && &bind == &c.Morph()) || (forward && &bind == &c.MoveUp()));
            bind.SetIsPressed(bind.IsDown() && !_wasDown[i]);
            bind.SetIsReleased(!bind.IsDown() && _wasDown[i]);
            _wasDown[i] = bind.IsDown();
        }
        player.ModNoteInput();
    }

    void PadProbe::Place(PlayerEntity& player, JumpPadEntity& pad, bool running, bool walking)
    {
        const Vector3 launch = pad.LaunchVelocity();
        const float along = std::sqrt(launch.X * launch.X + launch.Z * launch.Z);
        Vector3 way = along > 0.0F
            ? Vector3(launch.X / along, 0.0F, launch.Z / along)
            : Vector3(0.0F, 0.0F, 1.0F);
        if (walking)
        {
            for (std::int32_t turn = 0; turn < _approach; turn++)
            {
                way = Vector3(-way.Z, 0.0F, way.X);
            }
        }
        Vector3 at = OpenTK::Mathematics::AddY(static_cast<Vector3>(pad.Position), 0.02F);
        Vector3 speed{};
        if (running)
        {
            // On the pad, a step from its centre, running across it the way
            // it throws and faster than a hunter walks: Quake keeps none of
            // that. (From off the pad a hunter meets its base, which Quake
            // steps up and this game does not always.)
            at = Vector3(at.X - way.X * 0.4F, at.Y, at.Z - way.Z * 0.4F);
            speed = Vector3(way.X * 0.5F, 0.0F, way.Z * 0.5F);
        }
        if (walking)
        {
            // Off the pad, short of its volume, facing it: forward is held
            // until it throws, so the hunter has to get up onto it the way a
            // player does.
            const CollisionVolume volume = pad.ModVolume();
            const float half = (std::fabs(way.X) * volume.BoxDot1 + std::fabs(way.Z) * volume.BoxDot3) / 2.0F;
            // FP_PADPROBE_WALKBACK: start further off, for a pad at the top of
            // a slope (the start must stand on the slope, not inside it).
            float back = 0.6F;
            if (const char* walkBack = std::getenv("FP_PADPROBE_WALKBACK"))
            {
                back = static_cast<float>(std::atof(walkBack));
            }
            at = Vector3(at.X - way.X * (half + back), at.Y + 0.5F + back, at.Z - way.Z * (half + back));
        }
        player.Teleport(at, way, _scene->GetNodeRefByPosition(at));
        player.SetPrevPosition(player.Position);
        player.SetSpeed(speed);
    }

    void PadProbe::Step()
    {
        // No clock: the match would otherwise finish mid-sweep.
        GameState::MatchTime(-1);
        GameState::ForceEndGame(false);
        PlayerEntity& player = Runtime::RequireReference(Runtime::ManagedListAt(PlayerEntity::Players(), Slot));
        // FP_PADPROBE_LIQUID=x,y,z (Quake units): the hunter is put there
        // once spawned and its health logged as it changes, five seconds.
        if (const char* liquid = std::getenv("FP_PADPROBE_LIQUID"))
        {
            if (!TestFlag(player.LoadFlags(), LoadFlags::Spawned))
            {
                return;
            }
            static std::int32_t last = -1;
            if (!_placed)
            {
                float q[3]{};
                (void)std::sscanf(liquid, "%f,%f,%f", &q[0], &q[1], &q[2]);
                const float unit = 35.0F;
                const Vector3 at(q[0] / unit, q[2] / unit, -q[1] / unit);
                player.Teleport(at, Vector3(0.0F, 0.0F, 1.0F), _scene->GetNodeRefByPosition(at));
                player.SetPrevPosition(player.Position);
                player.SetSpeed(Vector3(0.0F, 0.0F, 0.0F));
                _placed = true;
            }
            if (player.Health() != last)
            {
                last = player.Health();
                Runtime::ConsoleWriteLine("LIQUID frame " + std::to_string(_frame) + " health " + std::to_string(last)
                    + " y " + F(static_cast<Vector3>(player.Position).Y * 35.0F, "F1"));
            }
            if (++_frame > 300 || player.Health() == 0)
            {
                Close();
            }
            return;
        }
        if (player.Health() == 0 || !TestFlag(player.LoadFlags(), LoadFlags::Active)
            || !TestFlag(player.LoadFlags(), LoadFlags::Spawned))
        {
            return;
        }

        if (_pads.empty())
        {
            for (auto pads = _scene->GetJumpPadEntities().GetEnumerator(); pads.MoveNext();)
            {
                JumpPadEntity* pad = pads.Current().get();
                if (pad != nullptr && pad->Q3Kind() == JumpPadEntity::Q3Trigger::Push)
                {
                    _pads.push_back(pad);
                }
                else if (pad != nullptr && pad->Q3Kind() == JumpPadEntity::Q3Trigger::Teleport)
                {
                    _teleporters.push_back(pad);
                }
            }
            if (_pads.empty() && _teleporters.empty())
            {
                Runtime::ConsoleWriteLine(_room + " has no Quake 3 jump pads.");
                Close();
                return;
            }
        }
        if (_trial >= _pads.size() * Starts)
        {
            const std::size_t index = _trial - _pads.size() * Starts;
            if (index >= _teleporters.size())
            {
                Close();
                return;
            }
            Teleport(player, *_teleporters[index]);
            return;
        }
        JumpPadEntity& pad = *_pads[_trial / Starts];
        const bool running = _trial % Starts == 1;
        const bool walking = _trial % Starts == 2;

        if (_alt != player.IsAltForm() || player.IsMorphing() || player.IsUnmorphing())
        {
            _frame = Runtime::UncheckedIncrement(_frame);
            Controls(player, !player.IsMorphing() && !player.IsUnmorphing() && _frame % 30 < 2, false);
            return;
        }
        if (!_placed)
        {
            Controls(player, false, false);
            // Still and on the ground for a while first: what the last
            // trial ended in -- a teleporter's carry -- is over.
            const Vector3 still = player.Speed();
            const bool settled = TestFlag(player.Flags1(), PlayerFlags1::Standing)
                && std::fabs(still.X) < 0.05F && std::fabs(still.Z) < 0.05F;
            _frame = settled ? Runtime::UncheckedIncrement(_frame) : 0;
            _waited = Runtime::UncheckedIncrement(_waited);
            // A ball may never sit quite still: five seconds is enough.
            if (_frame < Settle && _waited < 300)
            {
                return;
            }
            Place(player, pad, running, walking);
            _placed = true;
            _samples.clear();
            _waited = 0;
            return;
        }

        // Walking: forward held until the pad has thrown, as a player would.
        Controls(player, false, walking
            && std::none_of(_samples.begin(), _samples.end(),
                [](const Sample& s) { return s.Reset || s.Speed.Y > 0.3F; }));
        const Vector3 launch = pad.LaunchVelocity();
        const Vector3 speed = player.Speed();
        const float fall = pad.Q3Value() / 2.0F;
        Sample sample;
        sample.Position = player.Position;
        sample.Speed = speed;
        // The frame a pad threw (PlayerEntity::ActivateJumpPad).
        sample.Reset = player.TimeSinceJumpPad() <= 1;
        (void)launch;
        (void)fall;
        sample.Standing = TestFlag(player.Flags1(), PlayerFlags1::Standing);
        _samples.push_back(sample);
        _waited = Runtime::UncheckedIncrement(_waited);

        std::optional<std::size_t> last;
        for (std::size_t i = 0; i < _samples.size(); i++)
        {
            if (_samples[i].Reset)
            {
                last = i;
            }
        }
        const bool down = last.has_value() && _samples.size() - 1 > *last + 5 && sample.Standing;
        const bool elsewhere = walking && !last.has_value()
            && (sample.Speed.Y > 0.3F || _waited > 60 * 4);
        if (const char* trace = std::getenv("FP_PADPROBE_TRACE");
            elsewhere && trace != nullptr && std::atoi(trace) == pad.Id && !_samples.empty())
        {
            float low = _samples.front().Position.Y;
            float high = low;
            for (const Sample& each : _samples)
            {
                low = std::min(low, each.Position.Y);
                high = std::max(high, each.Position.Y);
            }
            const Vector3 from = _samples.front().Position;
            const Vector3 to = _samples.back().Position;
            Runtime::ConsoleWriteLine("    approach " + Runtime::ToString(_approach) + ": pad at "
                + F(pad.Position.X, "F2") + " " + F(pad.Position.Y, "F2") + " " + F(pad.Position.Z, "F2")
                + "  from " + F(from.X, "F2") + " " + F(from.Y, "F2") + " " + F(from.Z, "F2")
                + "  to " + F(to.X, "F2") + " " + F(to.Y, "F2") + " " + F(to.Z, "F2")
                + "  y " + F(low, "F2") + ".." + F(high, "F2"));
        }
        if (elsewhere && _approach < 3)
        {
            _approach++;
            _placed = false;
            _frame = 0;
            _waited = 0;
            return;
        }
        if (down || _waited > Limit || elsewhere)
        {
            _approach = 0;
            Measure(pad, running, walking);
            _trial++;
            _placed = false;
            _frame = 0;
            _waited = 0;
        }
    }

    // Stood on a Quake 3 teleporter: where it came out, against the
    // destination, and how it left, against Quake's 400 along its angle.
    void PadProbe::Teleport(PlayerEntity& player, JumpPadEntity& teleporter)
    {
        Controls(player, false, false);
        if (!_placed)
        {
            _frame = Runtime::UncheckedIncrement(_frame);
            if (_frame < Settle)
            {
                return;
            }
            const Vector3 at = OpenTK::Mathematics::AddY(static_cast<Vector3>(teleporter.Position), 0.02F);
            player.Teleport(at, Vector3(0.0F, 0.0F, 1.0F), _scene->GetNodeRefByPosition(at));
            player.SetPrevPosition(player.Position);
            player.SetSpeed(Vector3());
            _placed = true;
            _waited = 0;
            return;
        }
        _waited = Runtime::UncheckedIncrement(_waited);
        const Vector3 target = teleporter.Q3Target();
        const bool arrived = Distance(player.Position, target) < 2.0F;
        if (arrived || _waited > 120)
        {
            const float yaw = teleporter.Q3Value();
            const Vector3 exit = player.Speed();
            TeleportTrial trial;
            trial.Pad = teleporter.Id;
            trial.Arrived = arrived;
            trial.Offset = Distance(player.Position, target);
            trial.Facing = Vector3(std::sin(yaw), 0.0F, std::cos(yaw));
            trial.Exit = exit;
            _teleportTrials.push_back(trial);
            _trial++;
            _placed = false;
            _frame = 0;
            _waited = 0;
        }
    }

    void PadProbe::Measure(JumpPadEntity& pad, bool running, bool walking)
    {
        if (const char* trace = std::getenv("FP_PADPROBE_TRACE");
            trace != nullptr && std::atoi(trace) == pad.Id
            && running == (std::string_view(trace).find('r') != std::string_view::npos)
            && walking == (std::string_view(trace).find('w') != std::string_view::npos))
        {
            for (std::size_t i = 0; i < _samples.size(); i++)
            {
                const Sample& sample = _samples[i];
                Runtime::ConsoleWriteLine("    " + Right(Runtime::ToString(static_cast<std::int32_t>(i)), 4)
                    + "  " + F(sample.Position.X, "F3") + " " + F(sample.Position.Y, "F3") + " "
                    + F(sample.Position.Z, "F3") + "  v " + F(sample.Speed.X, "F4") + " "
                    + F(sample.Speed.Y, "F4") + " " + F(sample.Speed.Z, "F4")
                    + (sample.Reset ? "  reset" : "") + (sample.Standing ? "  standing" : ""));
            }
        }
        Trial trial;
        trial.Pad = pad.Id;
        trial.Running = running;
        trial.Walking = walking;
        std::optional<std::size_t> last;
        for (std::size_t i = 0; i < _samples.size(); i++)
        {
            if (_samples[i].Reset)
            {
                last = i;
            }
        }
        if (!last.has_value())
        {
            _trials.push_back(trial);
            return;
        }
        trial.Launched = true;
        const std::size_t k = *last;
        std::size_t land = _samples.size();
        for (std::size_t i = k + 6; i < _samples.size(); i++)
        {
            if (_samples[i].Standing)
            {
                land = i;
                break;
            }
        }
        trial.Landed = land < _samples.size();

        // The pad throws as the cartridge's do (PlayerEntity::ActivateJumpPad):
        // measured is how near the top of the flight comes to the top of
        // Quake's arc from the same launch point (the pad's target), and how
        // near the landing comes to where Quake's arc would have come down.
        const float g = pad.Q3Value();
        const Vector3 p0 = _samples[k].Position;
        const Vector3 v0 = _samples[k].Speed;
        const Vector3 quake = pad.LaunchVelocity();
        trial.SpeedError = 0.0F;
        std::size_t topAt = k;
        for (std::size_t i = k + 1; i < land; i++)
        {
            if (_samples[i].Position.Y > _samples[topAt].Position.Y)
            {
                topAt = i;
            }
        }
        trial.Height = _samples[topAt].Position.Y - p0.Y;
        if (g > 0.0F && quake.Y > 0.0F)
        {
            const float t = quake.Y / g;
            const Vector3 apex(p0.X + quake.X * t, p0.Y + quake.Y * quake.Y / (2.0F * g), p0.Z + quake.Z * t);
            trial.Deviation = Distance(_samples[topAt].Position, apex);
        }
        (void)v0;
        const std::size_t end = std::min(land, _samples.size() - 1);
        const Vector3 at = _samples[end].Position;
        trial.Frames = static_cast<std::int32_t>(end - k);
        trial.Distance = Across(at, p0);
        if (trial.Landed && g > 0.0F)
        {
            const float rise = p0.Y - at.Y;
            const float root = quake.Y * quake.Y + 2.0F * g * rise;
            if (root >= 0.0F)
            {
                const float t = (quake.Y + std::sqrt(root)) / g;
                trial.LandingError = Across(at, Vector3(p0.X + quake.X * t, 0.0F, p0.Z + quake.Z * t));
            }
        }
        _trials.push_back(trial);
    }

    std::int32_t PadProbe::Report() const
    {
        Runtime::ConsoleWriteLine();
        Runtime::ConsoleWriteLine(_room + ": " + ::MphRead::ToString(_hunter) + (_alt ? " (alt form)" : "")
            + ", " + Runtime::ToString(static_cast<std::int32_t>(_pads.size())) + " Quake 3 jump pads");
        Runtime::ConsoleWriteLine("  units; a frame is 1/30 s. Deviation: furthest from Quake's parabola in flight."
            " Landing: from where it meets the landing height (one tick of travel is the rounding);"
            " chained: thrown on by another trigger; hit: touched the level on the way.");
        Runtime::ConsoleWriteLine();
        Runtime::ConsoleWriteLine("     pad  start     time  distance  height   deviation  landing   speed kept");
        float worst = 0.0F;
        std::int32_t failed = 0;
        for (const Trial& trial : _trials)
        {
            if (!trial.Launched)
            {
                failed++;
                Runtime::ConsoleWriteLine("    " + Right(Runtime::ToString(trial.Pad), 4) + "  "
                    + (trial.Walking ? "walking" : trial.Running ? "running" : "standing") + "  never thrown");
                continue;
            }
            worst = std::max(worst, trial.Deviation);
            Runtime::ConsoleWriteLine("    " + Right(Runtime::ToString(trial.Pad), 4) + "  "
                + (trial.Walking ? "walking " : trial.Running ? "running " : "standing") + "  "
                + Right(F(static_cast<float>(trial.Frames) / 60.0F, "F2"), 5) + "s  "
                + Right(F(trial.Distance, "F2"), 8) + "  " + Right(F(trial.Height, "F2"), 6) + "   "
                + Right(F(trial.Deviation, "F5"), 9) + "  "
                + (trial.Landed ? Right(F(trial.LandingError, "F3"), 7)
                    : trial.Chained ? "chained" : trial.Hit ? "    hit" : " (none)") + "   "
                + Right(F(trial.SpeedError, "F5"), 9));
        }
        if (!_teleportTrials.empty())
        {
            Runtime::ConsoleWriteLine();
            Runtime::ConsoleWriteLine("     teleporter  arrived  from target  exit speed  exit angle off");
            for (const TeleportTrial& trial : _teleportTrials)
            {
                const float along = std::sqrt(trial.Exit.X * trial.Exit.X + trial.Exit.Z * trial.Exit.Z);
                const float cosine = along > 0.0F
                    ? (trial.Exit.X * trial.Facing.X + trial.Exit.Z * trial.Facing.Z) / along
                    : 0.0F;
                const float off = std::acos(std::clamp(cosine, -1.0F, 1.0F)) * 57.29578F;
                if (!trial.Arrived)
                {
                    failed++;
                }
                Runtime::ConsoleWriteLine("    " + Right(Runtime::ToString(trial.Pad), 11) + "  "
                    + (trial.Arrived ? "yes    " : "NO     ") + "  " + Right(F(trial.Offset, "F3"), 10)
                    + "  " + Right(F(along, "F4"), 10) + "  " + Right(F(off, "F2"), 9) + " deg");
            }
        }
        Runtime::ConsoleWriteLine();
        Runtime::ConsoleWriteLine("  furthest from Quake's path: " + F(worst, "F5") + " units"
            + (failed > 0 ? ", " + Runtime::ToString(failed) + " trials never thrown" : ""));
        return failed == 0 && worst < 0.01F ? 0 : 1;
    }

    std::int32_t PadProbe::Run(const std::string& room, Hunter hunter, bool alt)
    {
        try
        {
            std::unique_ptr<PadProbe> window(new PadProbe(room, hunter, alt));
            window->_window->Run(*window);
            return window->Report();
        }
        catch (const std::exception& ex)
        {
            Runtime::ConsoleWriteLine("PADPROBE " + room + " | " + Runtime::ExceptionTypeName(ex) + ": "
                + Runtime::ExceptionMessage(std::current_exception()));
            return 1;
        }
    }
}
