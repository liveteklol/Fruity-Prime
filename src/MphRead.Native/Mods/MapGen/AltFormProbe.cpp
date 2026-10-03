#include "AltFormProbe.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/OpenTK/GL.hpp"

#include "../../GameState.hpp"
#include "../../Scene.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Formats/Types.hpp"
#include "../Network/MapAudit.hpp"
#include "../Network/NetLaunch.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../NativeRuntime/Rhi/BackendFactory.hpp"

#include <algorithm>

namespace MphRead::Mods::MapGen
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Entities::Keybind;
    using Entities::LoadFlags;
    using Entities::PlayerControls;
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
    }

    std::int32_t AltFormProbe::MaxDelay()
    {
        return _traceDelay.value_or(40);
    }

    RendererPlatform::WindowSettings AltFormProbe::WindowSettings()
    {
        RendererPlatform::WindowSettings settings{};
        settings.UpdateFrequency = 60;
        settings.ClientSize = Vector2i(320, 180);
        settings.Title = "MphRead alt form probe";
        settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
        settings.Flags = RendererPlatform::WindowSettings::ContextFlags::Default;
        settings.ApiMajor = 3;
        settings.ApiMinor = 2;
        settings.StartVisible = false;
        settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::OpenGL;
        return settings;
    }

    AltFormProbe::AltFormProbe(std::string room, Vector3 start, Hunter hunter)
        : _window(RendererPlatform::CreateWindow(WindowSettings())),
          _room(std::move(room)),
          _start(start),
          _hunter(hunter)
    {
        NativeRuntime::Rhi::SwapchainDesc swapchainDesc{};
        const Vector2i framebufferSize = _window->Size();
        swapchainDesc.width = static_cast<std::uint32_t>(std::max(framebufferSize.X, 1));
        swapchainDesc.height = static_cast<std::uint32_t>(std::max(framebufferSize.Y, 1));
        _swapchain = NativeRuntime::Rhi::BackendFactory::CreateSwapchain(
            NativeRuntime::Rhi::GraphicsBackend::OpenGl, *_window, swapchainDesc);
        _delay = _traceDelay.value_or(0);
        Network::MapAudit::ForceEveryone(true);
        PlayerEntity::SetMaxPlayers(std::max<std::int32_t>(PlayerEntity::MaxPlayers(), Slot + 1));
        _scene = std::make_unique<MphRead::Scene>(ClientSize(), _window->Keyboard(), _window->Mouse(),
            [](std::string) {}, [this]()
            {
                Runtime::ConsoleWriteLine("    (the scene asked to close after trial "
                    + Runtime::ToString(_delay) + ")");
                Close();
            });
        // Slot 0 exists only to be the main player and stand still.
        _scene->AddPlayer(Hunter::Samus, 0, -1);
        _scene->AddPlayer(hunter, 0, -1);
        const auto& players = PlayerEntity::Players();
        for (std::size_t i = 0; i < players.size(); i++)
        {
            // No AI anywhere: PlayerAi writes the same controls and wins.
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

    AltFormProbe::~AltFormProbe() = default;

    MphRead::Scene& AltFormProbe::Scene() noexcept
    {
        return *_scene;
    }

    Vector2i AltFormProbe::ClientSize() const
    {
        return _window->Size();
    }

    void AltFormProbe::Close()
    {
        _window->Close();
    }

    void AltFormProbe::Present()
    {
        _swapchain->Present();
    }

    void AltFormProbe::Run()
    {
        _window->Run(*this);
    }

    void AltFormProbe::OnLoad()
    {
        _scene->Size(ClientSize());
        _scene->OnLoad();
        _window->BaseOnLoad();
        ::MphRead::NativeRuntime::Rhi::ResetWindowViewport(ClientSize().X, ClientSize().Y);
        _scene->OnResize();
    }

    void AltFormProbe::OnRenderFrame(const RendererPlatform::FrameEventArgs& args)
    {
        GameState::ApplyPause();
        _scene->OnSimulationFrame();
        _scene->OnDrawFrame();
        if (!_scene->OnRenderFrame())
        {
            return;
        }
        Step();
        Present();
        _scene->AfterRenderFrame();
        _window->BaseOnRenderFrame(args);
        if (_delay > MaxDelay())
        {
            Close();
        }
    }

    void AltFormProbe::Step()
    {
        // No clock: the match would otherwise finish mid-sweep.
        GameState::MatchTime(-1);
        GameState::ForceEndGame(false);
        PlayerEntity& player = Runtime::RequireReference(Runtime::ManagedListAt(PlayerEntity::Players(), Slot));
        if (player.Health() == 0 || !TestFlag(player.LoadFlags(), LoadFlags::Active)
            || !TestFlag(player.LoadFlags(), LoadFlags::Spawned))
        {
            return;
        }
        PlayerControls& c = player.Controls();
        const auto& all = c.All();
        for (std::size_t i = 0; i < all.size(); i++)
        {
            Keybind& bind = Runtime::RequireReference(Runtime::ManagedAt(all, i));
            bind.SetIsDown(false);
            bind.SetIsPressed(false);
            bind.SetIsReleased(false);
        }
        if (!_armed)
        {
            // A reset that cannot finish is the one way this probe can hang:
            // say so and stop, with what has been measured intact.
            _resetFrames = Runtime::UncheckedIncrement(_resetFrames);
            if (_resetFrames > 600)
            {
                Runtime::ConsoleWriteLine("    (gave up resetting before trial " + Runtime::ToString(_delay) + ": "
                    + (player.IsAltForm() ? "alt" : player.IsMorphing() ? "morphing" : "unmorphing")
                    + " at y " + F(player.Position.Y, "F3")
                    + (TestFlag(player.Flags1(), PlayerFlags1::NoUnmorph) ? ", cannot unmorph" : "") + ")");
                _delay = Runtime::UncheckedIncrement(MaxDelay());
                return;
            }
            if (player.IsMorphing() || player.IsUnmorphing())
            {
                // Let the animation finish; re-asking turns it round.
                _frame = Runtime::UncheckedIncrement(_frame);
                return;
            }
            if (player.IsAltForm())
            {
                if (_frame % 60 == 0)
                {
                    Place(player);
                }
                Press(player, c.Morph(), _frame % 60 == 20);
                Finish(player, c);
                _frame = Runtime::UncheckedIncrement(_frame);
                return;
            }
            // PrevPosition is moved with Position, or the sweep starts under
            // the box and the probe confirms itself.
            Place(player);
            _resetFrames = 0;
            _armed = true;
            _frame = 0;
            _lowest = _start.Y;
            _highest = _start.Y;
            _final = _start.Y;
            return;
        }
        _frame = Runtime::UncheckedIncrement(_frame);
        if (_frame < Settle)
        {
            return;
        }
        const std::int32_t t = Runtime::UncheckedSubtract(_frame, Settle);
        if (t == 0)
        {
            // Where he actually came to rest is what a fall is measured against.
            _rest = player.Position.Y;
            _lowest = _rest;
            _highest = _rest;
        }
        const bool jump = t >= 0 && t < 3;
        const bool morph = t >= _delay && t < Runtime::UncheckedAdd(_delay, 3);
        Press(player, c.Jump(), jump);
        Press(player, c.Morph(), morph);
        Finish(player, c);
        _lowest = Runtime::MathMin(_lowest, player.Position.Y);
        _highest = Runtime::MathMax(_highest, player.Position.Y);
        _final = player.Position.Y;
        if (_traceDelay == _delay && t <= Runtime::UncheckedAdd(_delay, 50))
        {
            const char* form = player.IsAltForm() ? "alt  " : player.IsMorphing() ? "morph"
                : player.IsUnmorphing() ? "unmrp" : "biped";
            Runtime::ConsoleWriteLine("    " + Right(Runtime::ToString(t), 4) + "  " + (jump ? "J" : " ")
                + (morph ? "M" : " ") + "  " + form + "  y " + Right(F(player.Position.Y, "F4"), 8)
                + "  prevY " + Right(F(player.PrevPosition().Y, "F4"), 8)
                + "  vy " + Right(F(player.Speed().Y, "F4"), 8) + "  "
                + (TestFlag(player.Flags1(), PlayerFlags1::Standing) ? "standing" : "        ") + "  "
                + (TestFlag(player.Flags1(), PlayerFlags1::NoUnmorph) ? "nounmorph" : ""));
        }
        if (t >= Runtime::UncheckedAdd(_delay, Observe))
        {
            const bool fell = _rest - _lowest > Through;
            _trials.emplace_back(_delay, _lowest, _highest, _final, fell);
            if (!_traceDelay.has_value())
            {
                Runtime::ConsoleWriteLine("    " + Right(Runtime::ToString(_delay), 5) + "  "
                    + Right(F(_highest, "F3"), 7) + "   " + Right(F(_lowest, "F3"), 8) + "   "
                    + Right(F(_final, "F3"), 7) + "   " + (fell ? "YES" : ""));
            }
            _delay = Runtime::UncheckedIncrement(_delay);
            _armed = false;
            _frame = 0;
        }
    }

    void AltFormProbe::Place(PlayerEntity& player)
    {
        player.Teleport(OpenTK::Mathematics::AddY(_start, 0.25F), Vector3(0, 0, 1), _scene->GetNodeRefByPosition(_start));
        player.SetPrevPosition(player.Position);
        player.SetSpeed(Vector3());
    }

    void AltFormProbe::Press(PlayerEntity& player, Keybind& bind, bool down)
    {
        (void)player;
        bind.SetIsDown(down);
    }

    void AltFormProbe::Finish(PlayerEntity& player, PlayerControls& c)
    {
        const auto& all = c.All();
        if (_wasDown.size() < all.size())
        {
            _wasDown.assign(all.size(), false);
        }
        bool any = false;
        for (std::size_t i = 0; i < all.size(); i++)
        {
            Keybind& bind = Runtime::RequireReference(Runtime::ManagedAt(all, i));
            // Edges, not levels.
            bind.SetIsPressed(bind.IsDown() && !_wasDown[i]);
            bind.SetIsReleased(!bind.IsDown() && _wasDown[i]);
            _wasDown[i] = bind.IsDown();
            any |= bind.IsDown() || bind.IsReleased();
        }
        if (any)
        {
            player.ModNoteInput();
        }
    }

    std::int32_t AltFormProbe::Report()
    {
        Runtime::ConsoleWriteLine();
        Runtime::ConsoleWriteLine(_room + ": " + ::MphRead::ToString(_hunter) + " at (" + F(_start.X, "F3") + ", "
            + F(_start.Y, "F3") + ", " + F(_start.Z, "F3") + ")");
        Runtime::ConsoleWriteLine();
        Runtime::ConsoleWriteLine("  jump, then morph N frames later");
        Runtime::ConsoleWriteLine("  came to rest at y " + F(_rest, "F3") + " before each jump");
        Runtime::ConsoleWriteLine();
        Runtime::ConsoleWriteLine("    delay   peak Y   lowest Y   final Y   fell through");
        std::int32_t fell = 0;
        for (const auto& [delay, lowest, highest, final, through] : _trials)
        {
            if (through)
            {
                fell++;
            }
            Runtime::ConsoleWriteLine("    " + Right(Runtime::ToString(delay), 5) + "  " + Right(F(highest, "F3"), 7) + "   "
                + Right(F(lowest, "F3"), 8) + "   " + Right(F(final, "F3"), 7) + "   " + (through ? "YES" : ""));
        }
        Runtime::ConsoleWriteLine();
        Runtime::ConsoleWriteLine(fell == 0 ? "  the floor held in every trial"
            : "  the floor gave way in " + Runtime::ToString(fell) + " of "
                + Runtime::ToString(static_cast<std::int32_t>(_trials.size())) + " trials");
        return fell == 0 ? 0 : 1;
    }

    std::int32_t AltFormProbe::Run(const std::string& room, Vector3 start, Hunter hunter)
    {
        std::unique_ptr<AltFormProbe> window;
        try
        {
            window = std::unique_ptr<AltFormProbe>(new AltFormProbe(room, start, hunter));
            window->Run();
            return window->Report();
        }
        catch (const std::exception& ex)
        {
            Runtime::ConsoleWriteLine("ALTPROBE " + room + " | " + Runtime::ExceptionTypeName(ex) + ": "
                + Runtime::ExceptionMessage(std::current_exception()));
            Runtime::ConsoleWriteLine();
            return 1;
        }
    }
}
