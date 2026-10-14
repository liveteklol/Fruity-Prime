#include "InstagibRails.hpp"

#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Buffers.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../Read.hpp"
#include "../../Scene.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
    using ::MphRead::BillboardMode;
    using ::MphRead::RenderItemType;
    using ::MphRead::NativeRuntime::ManagedAt;
    using ::MphRead::NativeRuntime::RequireReference;
    using ::OpenTK::Mathematics::Matrix4;
    using ::OpenTK::Mathematics::Vector3;
    using ::OpenTK::Mathematics::Vector4;

    constexpr float Tau = 6.28318530717958647692F;
    constexpr std::size_t MaxRails = 64;

    // How far a helix is wound before it is left to the core and the glow:
    // far enough to read as a spiral, short enough that a rail across a big
    // room is a few hundred quads and not a few thousand.
    constexpr float HelixReach = 60.0F;
    constexpr float HelixPitch = 1.2F;
    constexpr std::int32_t HelixStepsPerTurn = 8;

    struct Rail final
    {
        ::MphRead::Scene* Scene = nullptr;
        Vector3 Start{};
        Vector3 End{};
        Vector3 Color{};
        double Born = 0.0;
        float Phase = 0.0F;
    };

    // What a drawn trail needs of the game's "trail" model: its texture, and
    // the way the beams' own trails sample it.
    struct TrailTexture final
    {
        ::MphRead::Scene* Scene = nullptr;
        std::int32_t Binding = -1;
        ::MphRead::RepeatMode XRepeat = ::MphRead::RepeatMode::Clamp;
        ::MphRead::RepeatMode YRepeat = ::MphRead::RepeatMode::Clamp;
        float ScaleS = 1.0F;
        float ScaleT = 1.0F;
        float UvS = 1.0F;
        float UvT = 1.0F;
        bool Ready = false;
    };

    std::vector<Rail> g_rails;
    TrailTexture g_texture;
    std::uint32_t g_serial = 0;
    // A picture held still for checking the look (InstagibRails::ShowDemo).
    bool g_demo = false;
    float g_demoAge = 0.0F;

    [[nodiscard]] double Now() noexcept
    {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    [[nodiscard]] Vector3 Make(float x, float y, float z) noexcept { return Vector3(x, y, z); }
    [[nodiscard]] Vector3 Plus(Vector3 a, Vector3 b) noexcept { return Make(a.X + b.X, a.Y + b.Y, a.Z + b.Z); }
    [[nodiscard]] Vector3 Minus(Vector3 a, Vector3 b) noexcept { return Make(a.X - b.X, a.Y - b.Y, a.Z - b.Z); }
    [[nodiscard]] Vector3 Times(Vector3 a, float s) noexcept { return Make(a.X * s, a.Y * s, a.Z * s); }
    [[nodiscard]] float Dot(Vector3 a, Vector3 b) noexcept { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
    [[nodiscard]] Vector3 Cross(Vector3 a, Vector3 b) noexcept
    {
        return Make(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
    }
    [[nodiscard]] Vector3 Unit(Vector3 a) noexcept
    {
        const float length = std::sqrt(Dot(a, a));
        return length > 1.0e-6F ? Times(a, 1.0F / length) : Make(0.0F, 1.0F, 0.0F);
    }
    [[nodiscard]] Matrix4 Translation(Vector3 p) noexcept
    {
        return Matrix4(Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0), Vector4(p, 1));
    }
    [[nodiscard]] Vector3 Mix(Vector3 a, Vector3 b, float t) noexcept
    {
        return Plus(Times(a, 1.0F - t), Times(b, t));
    }

    [[nodiscard]] bool PrepareTexture(::MphRead::Scene& scene)
    {
        if (g_texture.Ready && g_texture.Scene == &scene)
        {
            return true;
        }
        g_texture = TrailTexture{};
        try
        {
            const std::shared_ptr<::MphRead::ModelInstance> instance = ::MphRead::Read::GetModelInstance("trail");
            if (!instance)
            {
                return false;
            }
            ::MphRead::Model& model = RequireReference(instance->Model());
            const ::MphRead::Material& material = RequireReference(ManagedAt(RequireReference(model.Materials), 0));
            const ::MphRead::Recolor& recolor = RequireReference(ManagedAt(RequireReference(model.Recolors), 0));
            const ::MphRead::Texture& texture = ManagedAt(RequireReference(recolor.Textures), 0);
            g_texture.Binding = scene.BindGetTexture(
                instance->Model(), material.TextureId, material.PaletteId, 0);
            g_texture.XRepeat = material.XRepeat;
            g_texture.YRepeat = material.YRepeat;
            g_texture.ScaleS = material.ScaleS;
            g_texture.ScaleT = material.ScaleT;
            g_texture.UvS = (texture.Width - (1.0F / 16.0F)) / texture.Width;
            g_texture.UvT = (texture.Height - (1.0F / 16.0F)) / texture.Height;
            g_texture.Scene = &scene;
            g_texture.Ready = true;
        }
        catch (...)
        {
            g_texture = TrailTexture{};
        }
        return g_texture.Ready;
    }

    // One ribbon: a strip of points along a line, each drawn with a width of
    // its own, always across the line of sight to the camera so that it is
    // as thin from the side as from above. Drawn as the game draws a beam's
    // own trail, so it blends and sorts the same.
    void Ribbon(::MphRead::Scene& scene, Vector3 origin, Vector3 axis, const std::vector<Vector3>& centres,
        const std::vector<float>& halfWidths, Vector3 color, float alpha)
    {
        const std::int32_t segments = static_cast<std::int32_t>(centres.size());
        if (segments < 2 || alpha <= 0.004F)
        {
            return;
        }
        const Vector3 camera = scene.CameraPosition();
        const std::int32_t count = 4 * segments;
        auto points = ::MphRead::NativeRuntime::RentFromSharedArrayPool(count);
        for (std::int32_t i = 0; i < segments; ++i)
        {
            const float along = static_cast<float>(i) / static_cast<float>(segments - 1) * g_texture.UvS;
            const Vector3 centre = centres[static_cast<std::size_t>(i)];
            const Vector3 toCamera = Minus(camera, Plus(origin, centre));
            Vector3 across = Cross(axis, toCamera);
            across = Dot(across, across) > 1.0e-8F ? Unit(across) : Make(0.0F, 1.0F, 0.0F);
            const Vector3 offset = Times(across, halfWidths[static_cast<std::size_t>(i)]);
            const std::size_t at = static_cast<std::size_t>(4 * i);
            (*points)[at] = Vector3(along, 0.0F, 0.0F);
            (*points)[at + 1] = Minus(centre, offset);
            (*points)[at + 2] = Vector3(along, g_texture.UvT, 0.0F);
            (*points)[at + 3] = Plus(centre, offset);
        }
        scene.AddRenderItem(RenderItemType::TrailMulti, std::clamp(alpha, 0.0F, 1.0F),
            scene.GetNextPolygonId(), color, g_texture.XRepeat, g_texture.YRepeat,
            g_texture.ScaleS, g_texture.ScaleT, Translation(origin), points, g_texture.Binding,
            BillboardMode::None, count);
    }

    // How wide a part of the rail is, `reach` units out of the muzzle: nothing
    // at the gun, the full width a few units on. A rail that is as wide at
    // the muzzle as anywhere else is a wall across the screen for the player
    // who fired it, whose eye is a hand's breadth from where it starts.
    [[nodiscard]] float Taper(float s, float full, float reach) noexcept
    {
        const float t = std::clamp(s / reach, 0.0F, 1.0F);
        return full * (0.12F + 0.88F * t * (2.0F - t));
    }

    void Draw(::MphRead::Scene& scene, const Rail& rail, float age)
    {
        const ::MphRead::Mods::Render::InstagibRails::Look look
            = ::MphRead::Mods::Render::InstagibRails::LookAt(age);
        const Vector3 along = Minus(rail.End, rail.Start);
        const float length = std::sqrt(Dot(along, along));
        if (length < 0.05F)
        {
            return;
        }
        const Vector3 axis = Times(along, 1.0F / length);
        const Vector3 reference = std::fabs(axis.Y) > 0.95F ? Make(1.0F, 0.0F, 0.0F) : Make(0.0F, 1.0F, 0.0F);
        const Vector3 u = Unit(Cross(axis, reference));
        const Vector3 v = Cross(axis, u);

        // The straight part -- core and glow -- is a few points so it can taper.
        std::vector<Vector3> spine;
        std::vector<float> coreWidth;
        std::vector<float> bandWidth;
        std::vector<float> glowWidth;
        for (const float s : {0.0F, 0.5F, 1.5F, 4.0F, 9.0F, 20.0F, length})
        {
            if (s > length || (!spine.empty() && s <= 0.0F))
            {
                continue;
            }
            spine.push_back(Times(axis, s));
            coreWidth.push_back(Taper(s, 0.06F, 3.0F));
            bandWidth.push_back(Taper(s, 0.11F, 5.0F));
            glowWidth.push_back(Taper(s, 0.24F, 9.0F));
        }
        // Three layers under the helix, widest and faintest first: a soft glow,
        // a band of the shooter's colour at full strength, and a hot core. The
        // band is what makes the colour read from across a room; the core
        // alone was a hairline that the glow barely lifted.
        const Vector3 white = Make(1.0F, 1.0F, 1.0F);
        const Vector3 core = Mix(rail.Color, white, 0.55F);
        Ribbon(scene, rail.Start, axis, spine, glowWidth, rail.Color, look.Glow);
        Ribbon(scene, rail.Start, axis, spine, bandWidth, rail.Color, 0.85F * look.Core);
        Ribbon(scene, rail.Start, axis, spine, coreWidth, core, look.Core);

        // The double helix, wound out of the muzzle. It opens up from a point
        // so the beam is not wider at the gun than it is anywhere else, and
        // turns as it fades, which is most of why it looks alive.
        const float reach = std::min(length, HelixReach);
        const std::int32_t steps = std::max(
            4, static_cast<std::int32_t>(std::ceil(reach / HelixPitch * HelixStepsPerTurn)));
        const float spin = rail.Phase + age * static_cast<float>(::MphRead::Mods::Render::InstagibRails::LifeSeconds * 14.0);
        std::vector<Vector3> centres(static_cast<std::size_t>(steps + 1));
        std::vector<float> widths(static_cast<std::size_t>(steps + 1));
        const Vector3 bright = Make(std::min(1.0F, rail.Color.X * 1.1F + 0.12F),
            std::min(1.0F, rail.Color.Y * 1.1F + 0.12F), std::min(1.0F, rail.Color.Z * 1.1F + 0.12F));
        for (std::int32_t strand = 0; strand < 2; ++strand)
        {
            for (std::int32_t i = 0; i <= steps; ++i)
            {
                const float s = reach * static_cast<float>(i) / static_cast<float>(steps);
                const float opening = std::min(1.0F, std::sqrt(s / 4.0F));
                const float radius = look.SpiralRadius * opening;
                const float angle = Tau * s / HelixPitch + spin + (strand == 1 ? Tau * 0.5F : 0.0F);
                const Vector3 radial = Plus(Times(u, std::cos(angle)), Times(v, std::sin(angle)));
                centres[static_cast<std::size_t>(i)] = Plus(Times(axis, s), Times(radial, radius));
                widths[static_cast<std::size_t>(i)] = Taper(s, 0.03F, 4.0F);
            }
            Ribbon(scene, rail.Start, axis, centres, widths, bright, look.Spiral);
        }
    }
}


namespace MphRead::Mods::Render
{
    namespace
    {
        const Vector3 Palette[InstagibRails::PaletteSize] = {
            Vector3(0.10F, 0.85F, 1.00F), // cyan
            Vector3(1.00F, 0.10F, 0.85F), // magenta
            Vector3(0.45F, 1.00F, 0.10F), // lime
            Vector3(1.00F, 0.55F, 0.00F), // orange
            Vector3(1.00F, 1.00F, 0.25F), // yellow
            Vector3(1.00F, 0.06F, 0.10F), // red
            Vector3(0.45F, 0.30F, 1.00F), // violet
            Vector3(0.65F, 0.80F, 1.00F), // ice
        };
    }

    Vector3 InstagibRails::ColorForSlot(std::int32_t slot) noexcept
    {
        const std::int32_t index = slot < 0 ? 0 : slot % PaletteSize;
        return Palette[index];
    }

    InstagibRails::Look InstagibRails::LookAt(float age) noexcept
    {
        const float left = std::clamp(1.0F - age, 0.0F, 1.0F);
        Look look;
        look.Core = std::pow(left, 0.8F);
        look.Glow = 0.75F * std::pow(left, 1.5F);
        look.Spiral = 0.95F * std::pow(left, 0.9F);
        look.SpiralRadius = 0.11F * (1.0F + 1.8F * age);
        return look;
    }

    void InstagibRails::Add(Scene& scene, Vector3 start, Vector3 end, std::int32_t slot)
    {
        if (g_rails.size() >= MaxRails)
        {
            g_rails.erase(g_rails.begin());
        }
        Rail rail;
        rail.Scene = &scene;
        rail.Start = start;
        rail.End = end;
        rail.Color = ColorForSlot(slot);
        rail.Born = Now();
        // Every rail starts its helix at a different angle, so two fired in a
        // row do not look like one drawn twice.
        rail.Phase = static_cast<float>(g_serial++ % 16U) * (Tau / 16.0F);
        g_rails.push_back(rail);
    }

    void InstagibRails::AddRenderItems(Scene& scene)
    {
        if (g_rails.empty())
        {
            return;
        }
        const double now = Now();
        g_rails.erase(std::remove_if(g_rails.begin(), g_rails.end(), [&](const Rail& rail)
        {
            return rail.Scene != &scene || (!g_demo && now - rail.Born >= LifeSeconds);
        }), g_rails.end());
        if (g_rails.empty() || !PrepareTexture(scene))
        {
            return;
        }
        for (const Rail& rail : g_rails)
        {
            Draw(scene, rail, g_demo ? g_demoAge : static_cast<float>((now - rail.Born) / LifeSeconds));
        }
    }

    void InstagibRails::Clear() noexcept
    {
        g_rails.clear();
        g_texture = TrailTexture{};
    }

    void InstagibRails::DemoMode(bool on) noexcept { g_demo = on; }
    bool InstagibRails::DemoMode() noexcept { return g_demo; }

    void InstagibRails::ShowDemo(Scene& scene, Vector3 eye, Vector3 forward, float age)
    {
        g_demoAge = age;
        g_rails.erase(std::remove_if(g_rails.begin(), g_rails.end(),
            [&](const Rail& rail) { return rail.Scene == &scene; }), g_rails.end());
        const Vector3 ahead = Unit(Make(forward.X, 0.0F, forward.Z));
        const Vector3 right = Make(-ahead.Z, 0.0F, ahead.X);
        // From the muzzle: a little to the right of the eye and below it.
        const Vector3 muzzle = Plus(Plus(eye, Times(right, 0.35F)), Make(0.0F, -0.3F, 0.0F));
        for (std::int32_t slot = 0; slot < PaletteSize; ++slot)
        {
            const float yaw = (static_cast<float>(slot) - 3.5F) * 0.12F;
            const Vector3 dir = Plus(Times(ahead, std::cos(yaw)), Times(right, std::sin(yaw)));
            const float lift = (static_cast<float>(slot % 3) - 1.0F) * 0.06F;
            Add(scene, muzzle, Plus(muzzle, Plus(Times(dir, 45.0F), Make(0.0F, lift * 45.0F, 0.0F))), slot);
        }
    }
}
