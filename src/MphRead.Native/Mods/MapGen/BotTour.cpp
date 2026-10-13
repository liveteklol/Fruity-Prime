#include "BotTour.hpp"

#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/OpenTK/GL.hpp"
#include "../../Entities/EntityBase.hpp"
#include "../../Entities/RoomEntity.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Entities/Players/PlayerAi.hpp"
#include "../../Formats/Collision.hpp"
#include "../../Formats/NodeData.hpp"
#include "../../GameState.hpp"
#include "../../Scene.hpp"
#include "../Network/Testing/Maps/MapAudit.hpp"
#include "../Network/Integration/Launch/NetLaunch.hpp"
#include "../WorldEvents.hpp"
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../NativeRuntime/Rhi/BackendFactory.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace MphRead::Mods::MapGen
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;
        using Entities::LoadFlags;
        using Entities::PlayerEntity;
        using OpenTK::Mathematics::Vector2i;
        using OpenTK::Mathematics::Vector3;
        // A waypoint is reached within this, a bot is stuck while it stays
        // within this of where it was for StuckSeconds.
        constexpr float Reach = 1.5F;
        constexpr float StuckRadius = 1.0F;
        constexpr float StuckSeconds = 5.0F;

        [[nodiscard]] float Distance(Vector3 a, Vector3 b)
        {
            const Vector3 d = a - b;
            return std::sqrt(d.X * d.X + d.Y * d.Y + d.Z * d.Z);
        }
    }

    RendererPlatform::WindowSettings BotTour::WindowSettings()
    {
        RendererPlatform::WindowSettings settings{};
        settings.UpdateFrequency = 60;
        settings.ClientSize = Vector2i(320, 180);
        settings.Title = "MphRead bot tour";
        settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
        settings.Flags = RendererPlatform::WindowSettings::ContextFlags::Default;
        settings.ApiMajor = 3;
        settings.ApiMinor = 2;
        settings.StartVisible = false;
        settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::OpenGL;
        return settings;
    }

    BotTour::BotTour(std::string room, std::int32_t players, double seconds)
        : _window(RendererPlatform::CreateWindow(WindowSettings())),
          _room(std::move(room)),
          _players(std::clamp(players, 2, 8)),
          _frames(static_cast<std::int32_t>(seconds * 60.0))
    {
        NativeRuntime::Rhi::SwapchainDesc swapchainDesc{};
        const Vector2i framebufferSize = _window->Size();
        swapchainDesc.width = static_cast<std::uint32_t>(std::max(framebufferSize.X, 1));
        swapchainDesc.height = static_cast<std::uint32_t>(std::max(framebufferSize.Y, 1));
        _swapchain = NativeRuntime::Rhi::BackendFactory::CreateSwapchain(
            NativeRuntime::Rhi::GraphicsBackend::OpenGl, *_window, swapchainDesc);
        Network::MapAudit::ForceEveryone(true);
        WorldEvents::Watching(true);
        WorldEvents::Reset();
        PlayerEntity::SetMaxPlayers(std::max(PlayerEntity::MaxPlayers(), _players));
        _scene = std::make_unique<MphRead::Scene>(ClientSize(), _window->Keyboard(), _window->Mouse(),
            [](std::string) {}, [this]() { Close(); });
        for (std::int32_t i = 0; i < _players; ++i)
        {
            _scene->AddPlayer(static_cast<Hunter>(i % 7), 0, -1);
        }
        const auto& all = PlayerEntity::Players();
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            PlayerEntity& player = Runtime::RequireReference(all[i]);
            player.SetIsBot(i > 0);
            player.SetBotLevel(1);
            if (static_cast<std::int32_t>(i) >= _players)
            {
                player.SetLoadFlags(player.LoadFlags() & ~LoadFlags::Active);
            }
        }
        PlayerEntity::SetPlayerCount(_players);
        PlayerEntity::SetMainPlayerIndex(0);
        _scene->AddRoom(_room, GameMode::Battle, Network::NetLaunch::RoomPlayerCount());
        _bots.resize(static_cast<std::size_t>(_players));
    }

    BotTour::~BotTour() = default;

    Vector2i BotTour::ClientSize() const
    {
        return _window->Size();
    }

    void BotTour::Close()
    {
        _window->Close();
    }

    void BotTour::OnLoad()
    {
        _scene->Size(ClientSize());
        _scene->OnLoad();
        _window->BaseOnLoad();
        ::MphRead::NativeRuntime::Rhi::ResetWindowViewport(ClientSize().X, ClientSize().Y);
        _scene->OnResize();
    }

    void BotTour::OnRenderFrame(const RendererPlatform::FrameEventArgs& args)
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

    void BotTour::Step()
    {
        GameState::MatchTime(-1);
        GameState::ForceEndGame(false);
        if (_nodes.empty())
        {
            const std::shared_ptr<Entities::RoomEntity> room = _scene->Room();
            const std::shared_ptr<Formats::NodeData> data = room == nullptr ? nullptr : room->NodeData();
            if (data != nullptr && data->Data != nullptr)
            {
                for (const auto& one : *data->Data)
                {
                    for (const auto& two : *one)
                    {
                        for (const std::shared_ptr<Formats::NodeData3>& node : *two)
                        {
                            if (node != nullptr)
                            {
                                _nodes.push_back(node->Position);
                            }
                        }
                    }
                }
            }
            for (Bot& bot : _bots)
            {
                bot.Visited.assign(_nodes.size(), false);
            }
            for (auto entities = _scene->Entities().GetEnumerator(); entities.MoveNext();)
            {
                const std::shared_ptr<Entities::EntityBase> entity = entities.Current();
                if (entity == nullptr)
                {
                    continue;
                }
                _pads += entity->Type == EntityType::JumpPad ? 1 : 0;
                _teleporters += entity->Type == EntityType::Teleporter ? 1 : 0;
                _items += entity->Type == EntityType::ItemSpawn ? 1 : 0;
                _movers += entity->Type == EntityType::Q3Mover ? 1 : 0;
            }
        }
        const float dt = 1.0F / 60.0F;
        const auto& all = PlayerEntity::Players();
        for (std::int32_t slot = 1; slot < _players; ++slot)
        {
            PlayerEntity& player = Runtime::RequireReference(all[static_cast<std::size_t>(slot)]);
            Bot& bot = _bots[static_cast<std::size_t>(slot)];
            const bool alive = player.Health() > 0 && TestFlag(player.LoadFlags(), LoadFlags::Active)
                && TestFlag(player.LoadFlags(), LoadFlags::Spawned);
            if (!alive)
            {
                if (bot.WasAlive)
                {
                    ++bot.Deaths;
                }
                bot.WasAlive = false;
                bot.Seen = false;
                bot.Stuck = 0.0F;
                continue;
            }
            bot.WasAlive = true;
            const Vector3 at = player.Position;
            if (bot.Seen)
            {
                const float step = Distance(at, bot.Last);
                // A teleporter's jump is not walking.
                if (step < 3.0F)
                {
                    bot.Travelled += step;
                }
            }
            else
            {
                bot.Anchor = at;
            }
            if (const char* trace = std::getenv("FP_BOTTOUR_TRACE");
                trace != nullptr && std::atoi(trace) == slot && _frame % 30 == 0)
            {
                std::shared_ptr<Formats::NodeData3> from;
                std::shared_ptr<Formats::NodeData3> to;
                if (player.AiData != nullptr)
                {
                    std::tie(from, to) = player.AiData->TourNodes();
                }
                char line[260];
                std::snprintf(line, sizeof(line), "  t%5.1f bot %d at %7.2f %7.2f %7.2f from %7.2f %7.2f %7.2f to %7.2f %7.2f %7.2f",
                    _frame / 60.0F, slot, at.X, at.Y, at.Z,
                    from ? from->Position.X : 0.0F, from ? from->Position.Y : 0.0F, from ? from->Position.Z : 0.0F,
                    to ? to->Position.X : 0.0F, to ? to->Position.Y : 0.0F, to ? to->Position.Z : 0.0F);
                Runtime::ConsoleWriteLine(line);
            }
            bot.Last = at;
            bot.Seen = true;
            if (Distance(at, bot.Anchor) > StuckRadius)
            {
                bot.Anchor = at;
                bot.Stuck = 0.0F;
            }
            else
            {
                bot.Stuck += dt;
                if (bot.Stuck > bot.LongestStuck)
                {
                    bot.LongestStuck = bot.Stuck;
                    bot.StuckAt = bot.Anchor;
                }
            }
            for (std::size_t n = 0; n < _nodes.size(); ++n)
            {
                if (!bot.Visited[n] && Distance(at, _nodes[n]) < Reach)
                {
                    bot.Visited[n] = true;
                }
            }
            const auto& standing = player.StandingEntCol();
            const bool riding = standing != nullptr && standing->Entity != nullptr
                && standing->Entity->Type == EntityType::Q3Mover;
            if (riding && !bot.WasRiding)
            {
                ++bot.Rides;
            }
            bot.WasRiding = riding;
        }
        if (++_frame >= _frames)
        {
            Close();
        }
    }

    std::int32_t BotTour::Report() const
    {
        Runtime::ConsoleWriteLine("== " + _room + ": " + std::to_string(_players - 1) + " bots, "
            + std::to_string(_frames / 60) + " s, " + std::to_string(_nodes.size()) + " waypoints, "
            + std::to_string(_pads) + " jump pads, " + std::to_string(_teleporters) + " teleporters, "
            + std::to_string(_items) + " item spawns, " + std::to_string(_movers) + " movers");
        Runtime::ConsoleWriteLine("   bot  travelled  reached  pads  teles  items  rides  deaths  stuck");
        std::vector<bool> any(_nodes.size(), false);
        std::int32_t pads = 0;
        std::int32_t teles = 0;
        std::int32_t items = 0;
        std::int32_t rides = 0;
        float stuck = 0.0F;
        for (std::int32_t slot = 1; slot < _players; ++slot)
        {
            const Bot& bot = _bots[static_cast<std::size_t>(slot)];
            std::int32_t reached = 0;
            for (std::size_t n = 0; n < _nodes.size(); ++n)
            {
                reached += bot.Visited[n] ? 1 : 0;
                any[n] = any[n] || bot.Visited[n];
            }
            const std::int32_t botPads = WorldEvents::JumpPadsFor(slot);
            const std::int32_t botTeles = WorldEvents::TeleportsFor(slot);
            const std::int32_t botItems = WorldEvents::PickupsFor(slot);
            pads += botPads;
            teles += botTeles;
            items += botItems;
            rides += bot.Rides;
            stuck = std::max(stuck, bot.LongestStuck);
            char line[200];
            std::snprintf(line, sizeof(line), "   %3d  %9.1f  %6.1f%%  %4d  %5d  %5d  %5d  %6d  %5.1fs",
                slot, bot.Travelled, _nodes.empty() ? 0.0 : 100.0 * reached / static_cast<double>(_nodes.size()),
                botPads, botTeles, botItems, bot.Rides, bot.Deaths, bot.LongestStuck);
            Runtime::ConsoleWriteLine(line);
            if (bot.LongestStuck >= StuckSeconds)
            {
                // In Quake's units and axes, for a Quake 3 level.
                char where[160];
                std::snprintf(where, sizeof(where), "         stuck longest at quake (%.0f %.0f %.0f)",
                    bot.StuckAt.X * 35.0F, -bot.StuckAt.Z * 35.0F, bot.StuckAt.Y * 35.0F);
                Runtime::ConsoleWriteLine(where);
            }
        }
        const std::int32_t together = static_cast<std::int32_t>(std::count(any.begin(), any.end(), true));
        char line[240];
        std::snprintf(line, sizeof(line),
            "   together: %.1f%% of the waypoints, %d pad throws, %d teleports, %d items, %d rides; longest stuck %.1fs",
            _nodes.empty() ? 0.0 : 100.0 * together / static_cast<double>(_nodes.size()), pads, teles, items, rides, stuck);
        Runtime::ConsoleWriteLine(line);
        const bool padsOk = _pads == 0 || pads > 0;
        const bool itemsOk = _items == 0 || items > 0;
        return padsOk && itemsOk && stuck < 30.0F ? 0 : 1;
    }

    std::int32_t BotTour::Run(const std::string& room, std::int32_t players, double seconds)
    {
        try
        {
            std::unique_ptr<BotTour> window(new BotTour(room, players, seconds));
            window->_window->Run(*window);
            return window->Report();
        }
        catch (const std::exception& ex)
        {
            Runtime::ConsoleWriteLine("BOTTOUR " + room + " | " + Runtime::ExceptionTypeName(ex) + ": "
                + Runtime::ExceptionMessage(std::current_exception()));
            return 1;
        }
    }
}
