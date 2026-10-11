#pragma once

#include "../../Renderer.hpp"
#include "../../Formats/Enums.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Entities
{
    class JumpPadEntity;
    class PlayerEntity;
    class PlayerControls;
}

namespace MphRead::Mods::MapGen
{
    // Sends one hunter off every Quake 3 jump pad of a room, from standing
    // on it and from running onto it, and measures the flight against
    // Quake's: from the last frame the pad set the velocity, the path has to
    // be the parabola of that velocity under the pad's gravity, frame for
    // frame, until something is hit. Runs the real engine with nobody
    // watching.
    // Usage: -padprobe "Q3DM17R" [-hunter Weavel] [-alt]
    class PadProbe final : public MphRead::RendererPlatform::WindowEvents
    {
    public:
        struct Trial final
        {
            std::int32_t Pad = 0;
            bool Running = false;
            // Walked onto it from off it, forward held.
            bool Walking = false;
            bool Launched = false;
            bool Landed = false;
            // Thrown on by another trigger before it came down.
            bool Chained = false;
            // Touched the level before it came down.
            bool Hit = false;
            std::int32_t Frames = 0;
            float Distance = 0.0F;
            float Height = 0.0F;
            // The furthest the path strayed from the parabola, and how far
            // from where the parabola meets the landing height it came down.
            float Deviation = 0.0F;
            float LandingError = 0.0F;
            // The speed along the ground the pad gave, against its own.
            float SpeedError = 0.0F;
        };

        struct TeleportTrial final
        {
            std::int32_t Pad = 0;
            bool Arrived = false;
            float Offset = 0.0F;
            OpenTK::Mathematics::Vector3 Facing{};
            OpenTK::Mathematics::Vector3 Exit{};
        };

    private:
        struct Sample final
        {
            OpenTK::Mathematics::Vector3 Position{};
            OpenTK::Mathematics::Vector3 Speed{};
            bool Reset = false;
            bool Standing = false;
        };

        std::shared_ptr<MphRead::RendererPlatform::Window> _window;
        std::unique_ptr<MphRead::NativeRuntime::Rhi::Swapchain> _swapchain;
        std::unique_ptr<MphRead::Scene> _scene;

        const std::string _room;
        const Hunter _hunter;
        const bool _alt;

        static constexpr std::int32_t Slot = 1;
        static constexpr std::int32_t Settle = 20;
        static constexpr std::int32_t Limit = 60 * 12;
        // Standing on it, running across it, walking onto it.
        static constexpr std::size_t Starts = 3;

        std::vector<Entities::JumpPadEntity*> _pads;
        std::vector<Entities::JumpPadEntity*> _teleporters;
        std::vector<TeleportTrial> _teleportTrials;
        std::size_t _trial = 0;
        std::int32_t _frame = 0;
        std::int32_t _waited = 0;
        bool _placed = false;
        // Which side a walking start comes from: what is behind one pad may
        // be another pad, or a drop.
        std::int32_t _approach = 0;
        std::vector<Sample> _samples;
        std::vector<Trial> _trials;
        std::vector<bool> _wasDown;

        [[nodiscard]] static MphRead::RendererPlatform::WindowSettings WindowSettings();

        PadProbe(std::string room, Hunter hunter, bool alt);

        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientSize() const;
        void Close();
        void Step();
        void Place(Entities::PlayerEntity& player, Entities::JumpPadEntity& pad, bool running, bool walking);
        void Measure(Entities::JumpPadEntity& pad, bool running, bool walking);
        void Teleport(Entities::PlayerEntity& player, Entities::JumpPadEntity& teleporter);
        void Controls(Entities::PlayerEntity& player, bool morph, bool forward);
        [[nodiscard]] std::int32_t Report() const;

    protected:
        void OnLoad() override;
        void OnRenderFrame(const MphRead::RendererPlatform::FrameEventArgs& args) override;

    public:
        PadProbe(const PadProbe&) = delete;
        PadProbe& operator=(const PadProbe&) = delete;
        ~PadProbe() override;

        [[nodiscard]] static std::int32_t Run(const std::string& room, Hunter hunter, bool alt);
    };
}
