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
    // Stands a hunter on each Quake 3 mover of a room that has collision
    // (Q3MoverEntity) and watches six seconds: whether it rides it, as on
    // a cartridge platform, or falls through or is left behind. Runs the
    // real engine with nobody watching.
    // Usage: -moverprobe "Q3DM19"
    class MoverProbe final : public MphRead::RendererPlatform::WindowEvents
    {
    public:
        static std::int32_t Run(const std::string& room);

        explicit MoverProbe(std::string room);
        ~MoverProbe() override;

        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientSize() const;
        void Close();
        void OnLoad() override;
        void OnRenderFrame(const RendererPlatform::FrameEventArgs& args) override;
        [[nodiscard]] static RendererPlatform::WindowSettings WindowSettings();

    private:
        struct Trial final
        {
            std::int32_t Model = 0;
            // How far the mover's top travelled, and the furthest the
            // hunter's feet were from it while it did.
            float Travel = 0.0F;
            float Worst = 0.0F;
            bool Fell = false;
            // Where it first stopped being carried: the frame, the mover's
            // top and its offset from where the level has it.
            std::int32_t LostFrame = -1;
            float LostTop = 0.0F;
            float LostOffset = 0.0F;
        };

        void Step();

        std::shared_ptr<MphRead::RendererPlatform::Window> _window;
        std::unique_ptr<MphRead::NativeRuntime::Rhi::Swapchain> _swapchain;
        std::unique_ptr<MphRead::Scene> _scene;
        const std::string _room;
        std::vector<std::shared_ptr<Entities::EntityBase>> _movers;
        std::size_t _current = 0;
        std::int32_t _frame = -1;
        float _lowest = 0.0F;
        float _highest = 0.0F;
        float _offLow = 0.0F;
        float _offHigh = 0.0F;
        OpenTK::Mathematics::Vector3 _start{};
        std::vector<Trial> _trials;
    };
}
