#pragma once

#include "../../NativeRuntime/OpenTK/Mathematics.hpp"

#include <cstdint>
#include <vector>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Mods::Render
{
    // The look of an Instagib shot, after Quake 3's railgun: a bright core
    // that stays where the shot went and fades over most of a second, a wide
    // soft glow around it, and a double helix that winds out of the muzzle,
    // loosens as it fades and is drawn in the shooter's own colour -- so who
    // fired it is read off the beam, as it is in Quake.
    //
    // Nothing here is simulation. Every machine draws the rails of the shots
    // it sees fired, from the same beam entities it already runs, so there is
    // nothing to send.
    class InstagibRails final
    {
    public:
        InstagibRails() = delete;

        // How long a rail is drawn, in seconds.
        static constexpr double LifeSeconds = 0.9;
        static constexpr std::int32_t PaletteSize = 8;

        // The colour of a player's shots: one saturated colour per slot, none
        // of them close enough to another to be mistaken for it at a glance.
        [[nodiscard]] static ::OpenTK::Mathematics::Vector3 ColorForSlot(std::int32_t slot) noexcept;

        // Leave a rail from start to end in this player's colour.
        static void Add(Scene& scene, ::OpenTK::Mathematics::Vector3 start,
            ::OpenTK::Mathematics::Vector3 end, std::int32_t slot);

        // Hand every living rail to the renderer. Once per drawn frame.
        static void AddRenderItems(Scene& scene);

        static void Clear() noexcept;

        // A fixed picture for checking the look without a fight to wait for:
        // eight rails, one in each colour, fanned out from the main player's
        // muzzle and held at one age. `-instagibdemo` with -maptest -shots.
        static void DemoMode(bool on) noexcept;
        [[nodiscard]] static bool DemoMode() noexcept;
        static void ShowDemo(Scene& scene, ::OpenTK::Mathematics::Vector3 eye,
            ::OpenTK::Mathematics::Vector3 forward, float age);

        // Where a rail is at a given age (0 = fired, 1 = gone): exposed so the
        // curve can be checked without a window.
        struct Look final
        {
            float Core = 0.0F;
            float Glow = 0.0F;
            float Spiral = 0.0F;
            float SpiralRadius = 0.0F;
        };
        [[nodiscard]] static Look LookAt(float age) noexcept;
    };
}
