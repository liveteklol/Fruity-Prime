#pragma once

#include "../../Renderer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Mods::MapGen
{
    // Lets bots play a room for a while and says how they got about it:
    // how far each went, how much of the level's waypoints (the bots' node
    // data) they reached between them, the jump pads and teleporters they
    // took, the items they picked up, the movers they rode, and the longest
    // any of them stood stuck. Runs the real engine with nobody watching.
    // Usage: -bottour "Q3DM17" [-players 6] [-seconds 120]
    class BotTour final : public MphRead::RendererPlatform::WindowEvents
    {
    public:
        static std::int32_t Run(const std::string& room, std::int32_t players, double seconds);

        BotTour(std::string room, std::int32_t players, double seconds);
        ~BotTour() override;

        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientSize() const;
        void Close();
        void OnLoad() override;
        void OnRenderFrame(const RendererPlatform::FrameEventArgs& args) override;
        [[nodiscard]] static RendererPlatform::WindowSettings WindowSettings();

    private:
        struct Bot final
        {
            float Travelled = 0.0F;
            std::int32_t Deaths = 0;
            std::int32_t Rides = 0;
            float LongestStuck = 0.0F;
            // Where it stood stuck longest.
            OpenTK::Mathematics::Vector3 StuckAt{};
            float Stuck = 0.0F;
            OpenTK::Mathematics::Vector3 Last{};
            OpenTK::Mathematics::Vector3 Anchor{};
            bool Seen = false;
            bool WasAlive = false;
            bool WasRiding = false;
            std::vector<bool> Visited;
        };

        void Step();
        [[nodiscard]] std::int32_t Report() const;

        std::shared_ptr<MphRead::RendererPlatform::Window> _window;
        std::unique_ptr<MphRead::NativeRuntime::Rhi::Swapchain> _swapchain;
        std::unique_ptr<MphRead::Scene> _scene;
        const std::string _room;
        const std::int32_t _players;
        const std::int32_t _frames;
        std::int32_t _frame = 0;
        std::vector<OpenTK::Mathematics::Vector3> _nodes;
        std::vector<Bot> _bots;
        std::int32_t _pads = 0;
        std::int32_t _teleporters = 0;
        std::int32_t _items = 0;
        std::int32_t _movers = 0;
    };
}
