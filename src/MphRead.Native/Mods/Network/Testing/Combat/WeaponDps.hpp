#pragma once

#include "Renderer.hpp"

#include "Formats/Enums.hpp"


#include <cstdint>
#include <memory>
#include <string>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    class WeaponDps final : public MphRead::RendererPlatform::WindowEvents
    {
    private:
        std::shared_ptr<MphRead::RendererPlatform::Window> _window;
        std::unique_ptr<MphRead::NativeRuntime::Rhi::Swapchain> _swapchain;

        [[nodiscard]] OpenTK::Mathematics::Vector2i ClientSize() const;
        void Close();
        void Present();

    public:
        void Run();

    private:
        const std::string _room;
        const Hunter _hunter;
        const BeamType _beam;
        const double _seconds;
        const float _distance;
        const bool _bombs;
        std::int32_t _frame = 0;
        std::int32_t _firingFrames = 0;
        std::int32_t _damage = 0;
        std::int32_t _hits = 0;
        std::int32_t _lastHealth = -1;
        std::int32_t _startHealth = 0;
        std::int32_t _killFrames = -1;
        std::int32_t _beamFrames = 0;
        std::int32_t _placedFrame = -1;
        bool _placed = false;
        std::int32_t _worstShockCoilTimer = 0;
        std::int32_t _lastAmmo = -1;
        std::int32_t _healed = 0;
        static constexpr std::int32_t HealHeadroom = 50;
        std::int32_t _lastHitFrame = -1;

        std::unique_ptr<MphRead::Scene> _scene;

        [[nodiscard]] static std::int32_t FullHealth(Entities::PlayerEntity& player);
        [[nodiscard]] static MphRead::RendererPlatform::WindowSettings GameSettings();
        [[nodiscard]] static MphRead::RendererPlatform::WindowSettings WindowSettings();

        WeaponDps(
            std::string room,
            Hunter hunter,
            BeamType beam,
            double seconds,
            float distance,
            bool bombs);

        [[nodiscard]] static bool Alive(Entities::PlayerEntity& player);
        void Account(Entities::PlayerEntity& victim);
        void Step();
        void HoldShooter(Entities::PlayerEntity& shooter);
        [[nodiscard]] std::int32_t Report();

    protected:
        void OnLoad() override;
        void OnRenderFrame(const MphRead::RendererPlatform::FrameEventArgs& args) override;

    public:
        WeaponDps(const WeaponDps&) = delete;
        WeaponDps& operator=(const WeaponDps&) = delete;
        WeaponDps(WeaponDps&&) = delete;
        WeaponDps& operator=(WeaponDps&&) = delete;
        ~WeaponDps() override;

        [[nodiscard]] MphRead::Scene& Scene() noexcept;

        [[nodiscard]] static std::int32_t Run(
            std::string room,
            Hunter hunter,
            BeamType beam,
            double seconds,
            float distance,
            bool bombs = false);
    };
}
