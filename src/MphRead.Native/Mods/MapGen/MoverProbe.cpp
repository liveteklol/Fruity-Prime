#include "MoverProbe.hpp"
#include "CustomRooms.hpp"

#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/OpenTK/GL.hpp"
#include "../../Entities/EntityBase.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Formats/Collision.hpp"
#include "../../Formats/Types.hpp"
#include "../../GameState.hpp"
#include "../../Scene.hpp"
#include "../Network/Testing/Maps/MapAudit.hpp"
#include "../Network/Integration/Launch/NetLaunch.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../NativeRuntime/Rhi/BackendFactory.hpp"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <limits>

namespace MphRead::Mods::MapGen
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;
        using Entities::LoadFlags;
        using Entities::PlayerEntity;
        using OpenTK::Mathematics::Vector2i;
        using OpenTK::Mathematics::Vector3;
        constexpr std::int32_t Slot = 1;
        constexpr std::int32_t Settle = 30;
        constexpr std::int32_t Frames = 360;

        // The mover's collision where it is now: the middle of its top and
        // how high that is.
        [[nodiscard]] bool Top(const Entities::EntityBase& mover, Vector3& middle)
        {
            const auto& entCol = mover.EntityCollision[0];
            if (entCol == nullptr || entCol->Collision == nullptr)
            {
                return false;
            }
            const auto& points = *entCol->Collision->Info->Points;
            if (points.empty())
            {
                return false;
            }
            float top = std::numeric_limits<float>::lowest();
            Vector3 sum(0.0F, 0.0F, 0.0F);
            for (const Vector3& point : points)
            {
                const Vector3 at = Matrix::Vec3MultMtx4(point, entCol->Transform);
                top = std::max(top, at.Y);
                sum = Vector3(sum.X + at.X, sum.Y, sum.Z + at.Z);
            }
            const float count = static_cast<float>(points.size());
            middle = Vector3(sum.X / count, top, sum.Z / count);
            return true;
        }
    }

    RendererPlatform::WindowSettings MoverProbe::WindowSettings()
    {
        RendererPlatform::WindowSettings settings{};
        settings.UpdateFrequency = 60;
        settings.ClientSize = Vector2i(320, 180);
        settings.Title = "MphRead mover probe";
        settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
        settings.Flags = RendererPlatform::WindowSettings::ContextFlags::Default;
        settings.ApiMajor = 3;
        settings.ApiMinor = 2;
        settings.StartVisible = false;
        settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::OpenGL;
        return settings;
    }

    MoverProbe::MoverProbe(std::string room)
        : _window(RendererPlatform::CreateWindow(WindowSettings())),
          _room(std::move(room))
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
        _scene->AddPlayer(Hunter::Samus, 0, -1);
        _scene->AddPlayer(Hunter::Samus, 0, -1);
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

    MoverProbe::~MoverProbe() = default;

    Vector2i MoverProbe::ClientSize() const
    {
        return _window->Size();
    }

    void MoverProbe::Close()
    {
        _window->Close();
    }

    void MoverProbe::OnLoad()
    {
        _scene->Size(ClientSize());
        _scene->OnLoad();
        _window->BaseOnLoad();
        ::MphRead::NativeRuntime::Rhi::ResetWindowViewport(ClientSize().X, ClientSize().Y);
        _scene->OnResize();
    }

    void MoverProbe::OnRenderFrame(const RendererPlatform::FrameEventArgs& args)
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

    void MoverProbe::Step()
    {
        GameState::MatchTime(-1);
        GameState::ForceEndGame(false);
        PlayerEntity& player = Runtime::RequireReference(Runtime::ManagedListAt(PlayerEntity::Players(), Slot));
        if (player.Health() == 0 || !TestFlag(player.LoadFlags(), LoadFlags::Active)
            || !TestFlag(player.LoadFlags(), LoadFlags::Spawned))
        {
            return;
        }
        if (_movers.empty() && _frame < 0)
        {
            for (auto entities = _scene->Entities().GetEnumerator(); entities.MoveNext();)
            {
                const std::shared_ptr<Entities::EntityBase> entity = entities.Current();
                if (entity != nullptr && entity->Type == EntityType::Q3Mover)
                {
                    _movers.push_back(entity);
                }
            }
            _frame = 0;
            if (_movers.empty())
            {
                Close();
                return;
            }
        }
        if (_current >= _movers.size())
        {
            Close();
            return;
        }
        Entities::EntityBase& mover = *_movers[_current];
        Vector3 top{};
        if (!Top(mover, top) || mover.EntityCollision[0]->MaxDistance < 0.5F)
        {
            ++_current;
            _frame = 0;
            return;
        }
        if (_frame == 0)
        {
            // FP_MOVERPROBE_DOORS: beside it instead, two units below its
            // top, where a door's trigger reaches: travel then says whether
            // it opened.
            const bool beside = std::getenv("FP_MOVERPROBE_DOORS") != nullptr;
            const Vector3 at = beside ? Vector3(top.X + 1.5F, top.Y - 2.0F, top.Z + 1.5F)
                                      : Vector3(top.X, top.Y + 0.3F, top.Z);
            player.Teleport(at, Vector3(0.0F, 0.0F, 1.0F), _scene->GetNodeRefByPosition(at));
            player.SetPrevPosition(player.Position);
            player.SetSpeed(Vector3(0.0F, 0.0F, 0.0F));
            _trials.push_back(Trial{});
            _trials.back().Model = mover.Id - 30000;
            _lowest = top.Y;
            _start = top;
            _highest = top.Y;
        }
        else if (_frame > Settle)
        {
            Trial& trial = _trials.back();
            const Vector3 feet = player.Position;
            // Carried, the hunter keeps where it stands on it: how far that
            // drifts, not how far below the mover's highest point it is.
            const float off = feet.Y - top.Y;
            if (_frame == Settle + 1)
            {
                _offLow = off;
                _offHigh = off;
            }
            if (std::getenv("FP_MOVERPROBE_TRACE") != nullptr && _trials.size() == 1 && (_frame % 10 == 0 || (_frame > 218 && _frame < 236)))
            {
                char line[200];
                std::snprintf(line, sizeof(line), "  f%3d top %8.3f feet %8.3f off %7.3f speedY %7.3f standing %d entcol %d T %7.3f inv2 %7.3f open %d riding %d lateral %d",
                    _frame, top.Y, feet.Y, off, player.Speed().Y,
                    TestFlag(player.Flags1(), Entities::PlayerFlags1::Standing) ? 1 : 0,
                    TestFlag(player.Flags1(), Entities::PlayerFlags1::CollidingEntity) ? 1 : 0, mover.EntityCollision[0]->Transform.M42, -mover.EntityCollision[0]->Inverse2.M42, CustomRooms::Q3Open(_scene->RoomId(), Vector3(feet.X, feet.Y + 0.3F, feet.Z)) ? 1 : 0, player.StandingEntCol() == mover.EntityCollision[0] ? 1 : 0, TestFlag(player.Flags1(), Entities::PlayerFlags1::CollidingLateral) ? 1 : 0);
                Runtime::ConsoleWriteLine(line);
            }
            if (trial.LostFrame < 0 && player.StandingEntCol() != mover.EntityCollision[0])
            {
                trial.LostFrame = _frame;
                trial.LostTop = top.Y;
                trial.LostOffset = mover.EntityCollision[0]->Transform.M42;
            }
            _offLow = std::min(_offLow, off);
            _offHigh = std::max(_offHigh, off);
            _lowest = std::min(_lowest, top.Y);
            _highest = std::max(_highest, top.Y);
            trial.Travel = std::max(trial.Travel, std::sqrt((top.X - _start.X) * (top.X - _start.X) + (top.Y - _start.Y) * (top.Y - _start.Y) + (top.Z - _start.Z) * (top.Z - _start.Z)));
            trial.Worst = _offHigh - _offLow;
            trial.Fell = trial.Fell || off < -3.0F;
        }
        if (++_frame > Frames)
        {
            ++_current;
            _frame = 0;
        }
    }

    std::int32_t MoverProbe::Run(const std::string& room)
    {
        try
        {
            std::unique_ptr<MoverProbe> window(new MoverProbe(room));
            window->_window->Run(*window);
            Runtime::ConsoleWriteLine("== " + room + ": " + std::to_string(window->_trials.size()) + " movers");
            Runtime::ConsoleWriteLine("   model    travel  feet off its top  ");
            std::int32_t failed = 0;
            for (const Trial& trial : window->_trials)
            {
                const bool rode = !trial.Fell && trial.Worst < 0.6F;
                failed += rode ? 0 : 1;
                char line[160];
                std::snprintf(line, sizeof(line), "   %5d  %8.2f  %8.3f  %s  (lost f%d top %.3f offset %.3f)", trial.Model, trial.Travel, trial.Worst,
                    trial.Fell ? "fell through" : (rode ? "rode it" : "left behind"), trial.LostFrame, trial.LostTop, trial.LostOffset);
                Runtime::ConsoleWriteLine(line);
            }
            return failed == 0 ? 0 : 1;
        }
        catch (const std::exception& ex)
        {
            Runtime::ConsoleWriteLine("MOVERPROBE " + room + " | " + Runtime::ExceptionTypeName(ex) + ": "
                + Runtime::ExceptionMessage(std::current_exception()));
            return 1;
        }
    }
}
